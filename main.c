#include <stdio.h>
#include <gtk/gtk.h>
#include </home/pablo/libfprint/libfprint/fprint.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct{
   struct fp_dev *dev;
   GtkWidget *parent_window;
}callback_enroll;

callback_enroll callback;
pthread_t enroll_thread;
GtkTextBuffer *buffer;
GAsyncQueue *message_queue;

void show_dialog(GtkWidget *parent_window, const char *format, ...);
struct fp_dscv_dev *discover_dev(struct fp_dscv_dev **discovered_devs, GtkWidget *parent_window);
void *enroll_finger_thread(void *data);
gboolean update_ui(void *user_data);
void start_enroll_process(GtkWidget *widget, void *data);
void new_enroll_window();

void show_dialog(GtkWidget *parent_window, const char *format, ... ){
  GtkWidget *dialog;
	va_list args;
	char *message;

	va_start(args, format);
	message = g_strdup_vprintf(format, args);

	dialog = gtk_message_dialog_new(GTK_WINDOW(parent_window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,GTK_BUTTONS_OK, "%s", message);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	g_free(message);
	va_end(args);
}

struct fp_dscv_dev *discover_dev(struct fp_dscv_dev **discovered_devs, GtkWidget *parent_window){
  struct fp_dscv_dev *ddev = discovered_devs[0];
	struct fp_driver *drv;
	if(!ddev){
    show_dialog(parent_window, "No devices were found...");
		return NULL;
	}
	drv = fp_dscv_dev_get_driver(ddev);
	show_dialog(parent_window, "Device found: %s", fp_driver_get_full_name(drv));
	return ddev;
}

GtkWidget *main_window;
GtkWidget *window;
GtkWidget *start_button;
GtkWidget *enroll_button;
GtkWidget *text_box;
GtkWidget *image;
GtkWidget *box;
struct fp_print_data *enrolled_print = NULL;

void new_enroll_window(){
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "Enroll");
	gtk_window_set_default_size(GTK_WINDOW(window), 600,800);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_container_add(GTK_CONTAINER(window), box);

	text_box = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text_box), FALSE);
	gtk_box_pack_start(GTK_BOX(box), text_box, TRUE, TRUE,0);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_box));
  image = gtk_image_new_from_file("enroll.pgm");
	//gtk_container_add(GTK_CONTAINER(window), image);

	start_button = gtk_button_new_with_label("Start");
	g_signal_connect(start_button, "clicked", G_CALLBACK(start_enroll_process), &callback);
	gtk_box_pack_start(GTK_BOX(box), start_button, FALSE, FALSE,0);
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	gtk_widget_show_all(window);
}

void *enroll_finger_thread(void *data){
  callback_enroll *callback = (callback_enroll*)data;
	struct fp_img *img = NULL;

	g_async_queue_push(message_queue, "You will to scan your finger 5 times...");

	int num = 0;
	while(num != FP_ENROLL_COMPLETE){
    num = fp_enroll_finger_img(callback->dev, &enrolled_print, &img);
		if(img){
      fp_img_save_to_file(img, "enroll.pgm");
			g_async_queue_push(message_queue, "Fingerprint saved...");
			fp_img_free(img);
		}
		if(num < 0){
      g_async_queue_push(message_queue, "Enroll while scanning fingerprint...");
			return NULL;
		}

		switch(num){
      case FP_ENROLL_COMPLETE:
			  g_async_queue_push(message_queue, "Enroll complete!");
			  gtk_button_set_label(GTK_BUTTON(start_button), "Finish");
				g_signal_handlers_disconnect_by_func(start_button, G_CALLBACK(new_enroll_window), NULL);
				g_signal_connect(start_button, "clicked", G_CALLBACK(gtk_main_quit), NULL);
				return NULL;
		  case FP_ENROLL_FAIL:
			  g_async_queue_push(message_queue, "Enroll Failed...");
				return NULL;
		  case FP_ENROLL_PASS:
			  g_async_queue_push(message_queue, "Enroll stage passed");
				break;
		  case FP_ENROLL_RETRY:
			  g_async_queue_push(message_queue, "Please try again");
				break;
		  case FP_ENROLL_RETRY_TOO_SHORT:
			  g_async_queue_push(message_queue, "Finger removed too fast, please try again");
				break;
      case FP_ENROLL_RETRY_CENTER_FINGER:
			  g_async_queue_push(message_queue, "Please center your finger and try again");
				break;
		}
	}
	g_async_queue_push(message_queue, "Enroll sucess!");
	return NULL;
}

gboolean update_ui(void *user_data){
  const char *message = (const char*)g_async_queue_try_pop(message_queue);
	if(message){
    gtk_text_buffer_set_text(buffer, message, -1);
	}
	gtk_image_set_from_file(GTK_IMAGE(image), "enroll.pgm");
	return G_SOURCE_CONTINUE;
}

void start_enroll_process(GtkWidget *widget, void *data){
	enrolled_print = NULL;
	pthread_create(&enroll_thread, NULL, enroll_finger_thread, data);
	g_timeout_add(100, update_ui,NULL);
}

int main(int argc, char *argv[]){
  gtk_init(&argc, &argv);

	main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(main_window), "Fingerprint program by Pablo D.");
	gtk_window_set_default_size(GTK_WINDOW(main_window), 300, 600);
	g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	struct fp_dscv_dev *ddev;
  struct fp_dscv_dev **discovered_dev;
  struct fp_dev *dev;

  int num = 1;  
	num = fp_init();
	if(num < 0){
    show_dialog(main_window, "Failed to intialize libfprint...");
		gtk_main_quit();
	}

	fp_set_debug(3);
	discovered_dev = fp_discover_devs();
	if(!discovered_dev){
    show_dialog(main_window, "Could not find devices...");
		fp_exit();
		gtk_main_quit();
	}
	ddev = discover_dev(discovered_dev, main_window);
	if(!ddev){
    show_dialog(main_window, "No devices found");
		fp_exit();
		gtk_main_quit();
	}

	dev = fp_dev_open(ddev);
	fp_dscv_devs_free(discovered_dev);
	if(!dev){
    show_dialog(main_window, "Could not open device");
		gtk_main_quit();
		fp_exit();
	}
	callback.parent_window = window;
	callback.dev = dev;
  enroll_button = gtk_button_new_with_label("Enroll");
  g_signal_connect(enroll_button, "clicked", G_CALLBACK(new_enroll_window), NULL);
	gtk_container_add(GTK_CONTAINER(main_window), enroll_button);
  gtk_widget_show_all(main_window);
  
  message_queue = g_async_queue_new();

	gtk_main();
	return 0;
}

