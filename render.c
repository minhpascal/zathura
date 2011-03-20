#include "render.h"
#include "zathura.h"

void* render_job(void* data);
bool render(zathura_page_t* page);

void*
render_job(void* data)
{
  render_thread_t* render_thread = (render_thread_t*) data;

  while (true) {
    g_mutex_lock(render_thread->lock);

    if (girara_list_size(render_thread->list) <= 0) {
      g_cond_wait(render_thread->cond, render_thread->lock);
    }

    zathura_page_t* page = (zathura_page_t*) girara_list_nth(render_thread->list, 0);
    girara_list_remove(render_thread->list, page);
    g_mutex_unlock(render_thread->lock);

    if (render(page) != true) {
      fprintf(stderr, "rendering failed\n");
    }

    printf("Rendered %d\n", page->number);
  }

  return NULL;
}

render_thread_t*
render_init(void)
{
  render_thread_t* render_thread = malloc(sizeof(render_thread_t));

  if (!render_thread) {
    goto error_ret;
  }

  /* init */
  render_thread->list   = NULL;
  render_thread->thread = NULL;
  render_thread->cond   = NULL;

  /* setup */
  render_thread->list = girara_list_new();

  if (!render_thread->list) {
    goto error_free;
  }

  render_thread->thread = g_thread_create(render_job, render_thread, TRUE, NULL);

  if (!render_thread->thread) {
    goto error_free;
  }

  render_thread->cond = g_cond_new();

  if (!render_thread->cond) {
    goto error_free;
  }

  render_thread->lock = g_mutex_new();

  if (!render_thread->lock) {
    goto error_free;
  }

  return render_thread;

error_free:

  if (render_thread->list) {
    girara_list_free(render_thread->list);
  }

  if (render_thread->cond) {
    g_cond_free(render_thread->cond);
  }

  if (render_thread->lock) {
    g_mutex_free(render_thread->lock);
  }

  free(render_thread);

error_ret:

  return NULL;
}

void
render_free(render_thread_t* render_thread)
{
  if (!render_thread) {
    return;
  }

  if (render_thread->list) {
    girara_list_free(render_thread->list);
  }

  if (render_thread->cond) {
    g_cond_free(render_thread->cond);
  }

  if (render_thread->lock) {
    g_mutex_free(render_thread->lock);
  }
}

bool
render_page(render_thread_t* render_thread, zathura_page_t* page)
{
  if (!render_thread || !page || !render_thread->list || page->rendered) {
    return false;
  }

  g_mutex_lock(render_thread->lock);
  if (!girara_list_contains(render_thread->list, page)) {
    girara_list_append(render_thread->list, page);
  }
  g_cond_signal(render_thread->cond);
  g_mutex_unlock(render_thread->lock);

  return true;
}

bool
render(zathura_page_t* page)
{
  gdk_threads_enter();
  g_static_mutex_lock(&(page->lock));
  zathura_image_buffer_t* image_buffer = zathura_page_render(page);

  if (image_buffer == NULL) {
    g_static_mutex_unlock(&(page->lock));
    gdk_threads_leave();
    return false;
  }

  /* remove old image */
  GtkWidget* widget = gtk_bin_get_child(GTK_BIN(page->event_box));
  if (widget != NULL) {
    gtk_container_remove(GTK_CONTAINER(page->event_box), widget);
  }

  /* create cairo surface */
  unsigned int page_width  = page->width  * Zathura.document->scale;
  unsigned int page_height = page->height * Zathura.document->scale;

  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, page_width, page_height);

  int rowstride        = cairo_image_surface_get_stride(surface);
  unsigned char* image = cairo_image_surface_get_data(surface);

  for (unsigned int y = 0; y < page_height; y++) {
    unsigned char* dst = image + y * rowstride;
    unsigned char* src = image_buffer->data + y * image_buffer->rowstride;

    for (unsigned int x = 0; x < page_width; x++) {
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst += 3;
    }
  }

  /* draw to gtk widget */
  GtkWidget* drawing_area = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(page->event_box), drawing_area);

  cairo_t* cairo = gdk_cairo_create(drawing_area->window);
  cairo_set_source_surface(cairo, surface, 0, 0);
  cairo_paint(cairo);
  cairo_destroy(cairo);

  zathura_image_buffer_free(image_buffer);
  g_static_mutex_unlock(&(page->lock));

  gdk_threads_leave();
  return true;
}

void
render_all(void)
{
  if (Zathura.document == NULL) {
    return;
  }

  /* unmark all pages */
  for (unsigned int page_id = 0; page_id < Zathura.document->number_of_pages; page_id++) {
    Zathura.document->pages[page_id]->rendered = false;
  }

  /* redraw current page */
  GtkAdjustment* view_vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(Zathura.UI.session->gtk.view));
  cb_view_vadjustment_value_changed(view_vadjustment, NULL);
}
