#include <stdio.h>
#include <gtk/gtk.h>
#include <libfprint/libfprint/fprint.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct{
   struct fp_dev *dev;
   GtkWidget *parent_window;
}callback_enroll;

typedef struct{
  struct fp_dev *dev;
	struct fp_print_data *data;
}callback_verify;

callback_enroll callback;
callback_verify callback_v;
pthread_t enroll_thread;
pthread_t verify_thread;
GtkTextBuffer *buffer;
GtkTextBuffer *buffer_v;
GAsyncQueue *message_queue;

void show_dialog(GtkWidget *parent_window, const char *format, ...);
struct fp_dscv_dev *discover_dev(struct fp_dscv_dev **discovered_devs, GtkWidget *parent_window);
void *enroll_finger_thread(void *data);
void *verify_finger_thread(void *data);
gboolean update_ui(void *user_data);
void start_enroll_process(GtkWidget *widget, void *data);
void new_enroll_window();
void new_verify_window();
void start_verify_process(GtkWidget *widget, void *data);

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
GtkWidget *window_v;
GtkWidget *start_button;
GtkWidget *enroll_button;
GtkWidget *verify_button;
GtkWidget *box_main;
GtkWidget *text_box;
GtkWidget *image;
GtkWidget *image_v;
GtkWidget *box;
GtkWidget *box_v;
GtkWidget *text_box_v;
GtkWidget *start_button_v;

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

	start_button = gtk_button_new_with_label("Start");
	g_signal_connect(start_button, "clicked", G_CALLBACK(start_enroll_process), &callback);
	gtk_box_pack_start(GTK_BOX(box), start_button, FALSE, FALSE,0);
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	gtk_widget_show_all(window);
}

void *enroll_finger_thread(void *data){
  callback_enroll *callback = (callback_enroll*)data;
	struct fp_print_data *enrolled_print = NULL;
	int num = 0;
	g_async_queue_push(message_queue, "You will to scan your finger 5 times...");

	while(num != FP_ENROLL_COMPLETE){
	  struct fp_img *img = NULL;
		g_async_queue_push(message_queue, "Scan your finger");
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

void *verify_finger_thread(void *data){
 callback_verify *callback_v = (callback_verify*)data;
 int num;

 while(1){
   struct fp_img *img;
	 g_async_queue_push(message_queue, "Scan your finger");
	 num = fp_verify_finger_img(callback_v->dev,callback_v->data, &img);
	 if(img){
     fp_img_save_to_file(img, "verify.pgm");
		 g_async_queue_push(message_queue, "Image saved");
		 fp_img_free(img);
	 }
	 if(num < 0){
     g_async_queue_push(message_queue, "Verification failed");
		 return NULL;
	 }
	 switch(num){
     case FP_VERIFY_NO_MATCH:
		   g_async_queue_push(message_queue, "NO MATCH");
			 fp_print_data_free(callback_v->data);
			 return NULL;
	   case FP_VERIFY_MATCH:
		   g_async_queue_push(message_queue, "MATCH");
			 fp_print_data_free(callback_v->data);
			 return NULL;
     case FP_VERIFY_RETRY_TOO_SHORT:
		   g_async_queue_push(message_queue, "Finger removed too fast, try again");
			 break;
     case FP_VERIFY_RETRY:
		   g_async_queue_push(message_queue, "Please try again");
			 break;
	   case FP_VERIFY_RETRY_CENTER_FINGER:
		   g_async_queue_push(message_queue, "Center your finger and try again");
			 break;
     case FP_VERIFY_RETRY_REMOVE_FINGER:
		   g_async_queue_push(message_queue, "Place your finger on the sensor");
			 break;
	 }
 }
}

gboolean update_ui(void *user_data){
  const char *message = (const char*)g_async_queue_try_pop(message_queue);
	if(message){
    gtk_text_buffer_set_text(buffer, message, -1);
	}
	gtk_image_set_from_file(GTK_IMAGE(image), "enroll.pgm");
	return G_SOURCE_CONTINUE;
}

gboolean update_v(void *user_data){
  const char *message = (const char*)g_async_queue_try_pop(message_queue);
  if(message){
    gtk_text_buffer_set_text(buffer_v, message, -1);
	}
	return G_SOURCE_CONTINUE;
}

void start_enroll_process(GtkWidget *widget, void *data){
	enrolled_print = NULL;
	pthread_create(&enroll_thread, NULL, enroll_finger_thread, data);
	g_timeout_add(100, update_ui,NULL);
}

static int num_v;

void start_verify_process(GtkWidget *widget, void *data){
  enrolled_print = NULL;
  if(num_v != 0){
    fprintf(stderr, "Failed to load fingerprint");
		fprintf(stderr, "Run enroll first");
		fp_dev_close(callback_v.dev);
	}
	pthread_create(&verify_thread, NULL, verify_finger_thread, data);
	g_timeout_add(100, update_v, NULL);
}


void new_verify_window(){
  window_v = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window_v), "Verify");
	gtk_window_set_default_size(GTK_WINDOW(window_v), 600, 800);
	g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  
  box_v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_container_add(GTK_CONTAINER(window_v), box_v);
	
	text_box_v = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text_box_v), FALSE);
	gtk_box_pack_start(GTK_BOX(box_v), text_box_v, TRUE, TRUE, 0);

  buffer_v = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_box_v));
	image_v = gtk_image_new_from_file("verify.pgm");
  start_button_v = gtk_button_new_with_label("Start");
	g_signal_connect(start_button_v, "clicked",G_CALLBACK(start_verify_process), &callback_v);
	gtk_box_pack_start(GTK_BOX(box_v), start_button_v, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box_v), image_v, FALSE, FALSE, 0);

  gtk_widget_show_all(window_v);

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
	struct fp_print_data *data;

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
	num_v = fp_print_data_load(dev, RIGHT_INDEX, &data);

	callback.parent_window = window;
	callback.dev = dev;
	callback_v.dev = dev;
	callback_v.data = data;
  enroll_button = gtk_button_new_with_label("Enroll");
	verify_button = gtk_button_new_with_label("Verify");
  g_signal_connect(enroll_button, "clicked", G_CALLBACK(new_enroll_window), NULL);
	g_signal_connect(verify_button, "clicked", G_CALLBACK(new_verify_window), NULL);

	box_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_container_add(GTK_CONTAINER(main_window), box_main);
	gtk_box_pack_start(GTK_BOX(box_main), enroll_button, FALSE, FALSE,0);
	gtk_box_pack_start(GTK_BOX(box_main), verify_button, FALSE, FALSE, 0);
  gtk_widget_show_all(main_window);
  
  message_queue = g_async_queue_new();

	gtk_main();
	return 0;
}

