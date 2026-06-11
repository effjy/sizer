/*
 * Sizer v2.0.1 - Disk Space Analyzer
 * A GTK3 GUI program that reports the files and directories taking the most
 * space on the system, starting from the / directory, with percentages and a
 * visual diagram.
 *
 * Author: Jean-Francois Lachance-Caumartin
 * License: MIT
 */

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_NAME      "Sizer"
#ifndef APP_VERSION
#define APP_VERSION   "2.0.1"
#endif
#define APP_ID        "com.jflc.sizer"
#define APP_AUTHOR    "Jean-Francois Lachance-Caumartin"

/* Maximum number of entries to display in the report. */
#define MAX_ROWS 200

/* ------------------------------------------------------------------ */
/* Data model                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    gchar   *path;     /* full path of the entry                       */
    gchar   *name;     /* display name (basename)                      */
    guint64  size;     /* size in bytes (recursive for directories)    */
    gboolean is_dir;   /* TRUE if directory                            */
} Entry;

typedef struct {
    GtkWidget    *window;
    GtkWidget    *path_entry;       /* shows current scan root          */
    GtkWidget    *scan_button;
    GtkWidget    *stop_button;
    GtkWidget    *up_button;
    GtkWidget    *progress;
    GtkWidget    *status_label;
    GtkWidget    *tree;
    GtkListStore *store;
    GtkWidget    *chart;            /* drawing area for the diagram     */
    GtkWidget    *total_label;

    gchar        *initial_scan;     /* path to auto-scan on launch, or NULL */
    gchar        *current_root;     /* directory currently displayed    */
    GArray       *entries;          /* array of Entry for current_root  */
    guint64       total_size;       /* total of all entries             */

    /* scanning state */
    gboolean      scanning;
    gboolean      cancel_requested;
    GThread      *worker;
    guint         pulse_id;
} AppState;

/* A neon "cyber" palette used by the chart and legend. */
static const gdouble palette[][3] = {
    {0.00, 0.90, 0.95}, /* cyan      */
    {0.95, 0.20, 0.60}, /* magenta   */
    {0.45, 0.95, 0.35}, /* neon lime */
    {0.55, 0.40, 1.00}, /* violet    */
    {1.00, 0.65, 0.10}, /* amber     */
    {0.20, 0.65, 1.00}, /* azure     */
    {1.00, 0.30, 0.35}, /* hot red   */
    {0.10, 0.95, 0.70}, /* mint      */
    {0.95, 0.85, 0.20}, /* gold      */
    {0.80, 0.35, 0.95}, /* purple    */
};
#define PALETTE_SIZE (sizeof(palette) / sizeof(palette[0]))

/* Cyber theme: a dark UI with neon cyan/magenta accents, applied globally
 * through a CSS provider so the whole application shares the look. */
static const char *CYBER_CSS =
    "* {"
    "  color: #d6f5ff;"
    "  font-family: 'DejaVu Sans Mono','Ubuntu Mono',monospace;"
    "}"
    "window, .background {"
    "  background-color: #0a0e16;"
    "}"
    "headerbar, .titlebar {"
    "  background: linear-gradient(180deg,#101826,#0a0e16);"
    "  border-bottom: 1px solid #00e6f2;"
    "}"
    "button {"
    "  background: linear-gradient(180deg,#13202e,#0c141d);"
    "  color: #9fe9ff;"
    "  border: 1px solid #1e3a4a;"
    "  border-radius: 6px;"
    "  padding: 4px 10px;"
    "  text-shadow: 0 0 4px rgba(0,230,242,0.35);"
    "}"
    "button:hover {"
    "  border-color: #00e6f2;"
    "  color: #ffffff;"
    "  background: linear-gradient(180deg,#16566b,#0e2630);"
    "}"
    "button:active { background: #00e6f2; color: #04141a; }"
    "button:disabled { color: #3a4a55; border-color: #15212b; }"
    "entry {"
    "  background-color: #0c141d;"
    "  color: #aef6ff;"
    "  border: 1px solid #1e3a4a;"
    "  border-radius: 6px;"
    "  caret-color: #00e6f2;"
    "  padding: 4px 8px;"
    "}"
    "entry:focus { border-color: #ff2fa0; }"
    "treeview {"
    "  background-color: #0a0e16;"
    "  color: #cdeefb;"
    "}"
    "treeview:selected {"
    "  background-color: #0e3a46;"
    "  color: #ffffff;"
    "}"
    "treeview header button {"
    "  background: #0d1a26;"
    "  color: #00e6f2;"
    "  border-radius: 0;"
    "  border-bottom: 1px solid #00e6f2;"
    "  font-weight: bold;"
    "}"
    "progressbar > trough {"
    "  background-color: #0c141d;"
    "  border: 1px solid #1e3a4a;"
    "  border-radius: 6px;"
    "  min-height: 14px;"
    "}"
    "progressbar > trough > progress {"
    "  background: linear-gradient(90deg,#00e6f2,#ff2fa0);"
    "  border-radius: 6px;"
    "}"
    "progressbar text { color: #d6f5ff; }"
    "label { text-shadow: 0 0 3px rgba(0,230,242,0.20); }"
    "paned separator {"
    "  background-color: #00e6f2;"
    "  min-width: 2px;"
    "}"
    "scrollbar slider {"
    "  background-color: #16566b;"
    "  border-radius: 8px;"
    "  min-width: 8px;"
    "}"
    "scrollbar slider:hover { background-color: #00e6f2; }"
    "tooltip { background-color: #0c141d; color: #aef6ff; }";

static void apply_cyber_theme(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CYBER_CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

/* Columns in the GtkListStore / GtkTreeView. */
enum {
    COL_NAME,
    COL_SIZE_HUMAN,
    COL_PERCENT,
    COL_BAR,          /* integer 0-100 for the cell progress bar       */
    COL_TYPE,
    COL_PATH,         /* hidden, full path for navigation              */
    COL_IS_DIR,       /* hidden boolean                                */
    COL_COLOR,        /* color string for the type cell                */
    N_COLUMNS
};

/* ------------------------------------------------------------------ */
/* Utility helpers                                                     */
/* ------------------------------------------------------------------ */

/* Format a byte count into a human readable string (e.g. "1.5 GB"). */
static gchar *human_size(guint64 bytes)
{
    const gchar *units[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    gdouble size = (gdouble) bytes;
    gint i = 0;
    while (size >= 1024.0 && i < 5) {
        size /= 1024.0;
        i++;
    }
    if (i == 0)
        return g_strdup_printf("%.0f %s", size, units[i]);
    return g_strdup_printf("%.2f %s", size, units[i]);
}

static void entry_clear(Entry *e)
{
    g_free(e->path);
    g_free(e->name);
}

/* Recursively compute the size of a directory subtree. Honors cancel. */
static guint64 compute_dir_size(const gchar *path, gboolean *cancel)
{
    guint64 total = 0;
    GDir *dir = g_dir_open(path, 0, NULL);
    if (!dir)
        return 0;

    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (cancel && *cancel)
            break;
        gchar *child = g_build_filename(path, name, NULL);
        GStatBuf st;
        if (g_lstat(child, &st) == 0) {
            if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
                total += compute_dir_size(child, cancel);
            } else {
                /* Use actually-allocated blocks (like du), not apparent
                 * size. This avoids absurd totals from sparse/pseudo files
                 * such as /proc/kcore which report a huge st_size but use
                 * zero disk blocks. */
                total += (guint64) st.st_blocks * 512;
            }
        }
        g_free(child);
    }
    g_dir_close(dir);
    return total;
}

static gint entry_compare_desc(gconstpointer a, gconstpointer b)
{
    const Entry *ea = a;
    const Entry *eb = b;
    if (ea->size < eb->size) return 1;
    if (ea->size > eb->size) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Scanning (runs in a worker thread)                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    AppState *app;
    gchar    *root;
    GArray   *entries;
    guint64   total;
} ScanResult;

/* Invoked on the main thread once a scan is complete. */
static gboolean scan_finished_cb(gpointer data)
{
    ScanResult *res = data;
    AppState *app = res->app;

    /* Replace the app's entries with the freshly computed ones. */
    if (app->entries) {
        for (guint i = 0; i < app->entries->len; i++)
            entry_clear(&g_array_index(app->entries, Entry, i));
        g_array_free(app->entries, TRUE);
    }
    app->entries = res->entries;
    app->total_size = res->total;

    g_free(app->current_root);
    app->current_root = g_strdup(res->root);
    gtk_entry_set_text(GTK_ENTRY(app->path_entry), app->current_root);

    /* Populate the tree view. */
    gtk_list_store_clear(app->store);
    guint shown = MIN(app->entries->len, MAX_ROWS);
    for (guint i = 0; i < shown; i++) {
        Entry *e = &g_array_index(app->entries, Entry, i);
        gdouble pct = app->total_size > 0
            ? (gdouble) e->size / (gdouble) app->total_size * 100.0
            : 0.0;
        gchar *hsize = human_size(e->size);
        gchar *pctstr = g_strdup_printf("%.1f%%", pct);
        const gdouble *col = palette[i % PALETTE_SIZE];
        gchar *colstr = g_strdup_printf("#%02x%02x%02x",
            (int)(col[0]*255), (int)(col[1]*255), (int)(col[2]*255));

        GtkTreeIter iter;
        gtk_list_store_append(app->store, &iter);
        gtk_list_store_set(app->store, &iter,
            COL_NAME,       e->name,
            COL_SIZE_HUMAN, hsize,
            COL_PERCENT,    pctstr,
            COL_BAR,        (gint) (pct + 0.5),
            COL_TYPE,       e->is_dir ? "Folder" : "File",
            COL_PATH,       e->path,
            COL_IS_DIR,     e->is_dir,
            COL_COLOR,      colstr,
            -1);
        g_free(hsize);
        g_free(pctstr);
        g_free(colstr);
    }

    gchar *htotal = human_size(app->total_size);
    gchar *summary = g_strdup_printf(
        "Total: %s across %u item%s",
        htotal, app->entries->len,
        app->entries->len == 1 ? "" : "s");
    gtk_label_set_text(GTK_LABEL(app->total_label), summary);
    g_free(htotal);
    g_free(summary);

    /* Stop the progress pulse. */
    if (app->pulse_id) {
        g_source_remove(app->pulse_id);
        app->pulse_id = 0;
    }
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress), 1.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress), "Done");

    gboolean cancelled = app->cancel_requested;
    gtk_label_set_text(GTK_LABEL(app->status_label),
        cancelled ? "Scan cancelled." : "Scan complete.");

    app->scanning = FALSE;
    app->cancel_requested = FALSE;
    gtk_widget_set_sensitive(app->scan_button, TRUE);
    gtk_widget_set_sensitive(app->stop_button, FALSE);
    gtk_widget_set_sensitive(app->up_button, TRUE);

    gtk_widget_queue_draw(app->chart);

    g_free(res->root);
    g_free(res);
    return G_SOURCE_REMOVE;
}

static gpointer scan_worker(gpointer data)
{
    ScanResult *res = data;
    AppState *app = res->app;

    GArray *entries = g_array_new(FALSE, FALSE, sizeof(Entry));
    guint64 total = 0;

    GDir *dir = g_dir_open(res->root, 0, NULL);
    if (dir) {
        const gchar *name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            if (app->cancel_requested)
                break;
            gchar *child = g_build_filename(res->root, name, NULL);
            GStatBuf st;
            if (g_lstat(child, &st) == 0) {
                Entry e;
                e.path = child; /* ownership transferred */
                e.name = g_strdup(name);
                e.is_dir = S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode);
                if (e.is_dir)
                    e.size = compute_dir_size(child, &app->cancel_requested);
                else
                    e.size = (guint64) st.st_blocks * 512;
                total += e.size;
                g_array_append_val(entries, e);
            } else {
                g_free(child);
            }
        }
        g_dir_close(dir);
    }

    g_array_sort(entries, entry_compare_desc);

    res->entries = entries;
    res->total = total;

    g_idle_add(scan_finished_cb, res);
    return NULL;
}

static gboolean pulse_cb(gpointer data)
{
    AppState *app = data;
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(app->progress));
    return G_SOURCE_CONTINUE;
}

static void start_scan(AppState *app, const gchar *root)
{
    if (app->scanning)
        return;
    if (!g_file_test(root, G_FILE_TEST_IS_DIR)) {
        gtk_label_set_text(GTK_LABEL(app->status_label),
            "Not a readable directory.");
        return;
    }

    app->scanning = TRUE;
    app->cancel_requested = FALSE;
    gtk_widget_set_sensitive(app->scan_button, FALSE);
    gtk_widget_set_sensitive(app->stop_button, TRUE);
    gtk_widget_set_sensitive(app->up_button, FALSE);

    gchar *msg = g_strdup_printf("Scanning %s ...", root);
    gtk_label_set_text(GTK_LABEL(app->status_label), msg);
    g_free(msg);

    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress), "Working");
    app->pulse_id = g_timeout_add(80, pulse_cb, app);

    ScanResult *res = g_new0(ScanResult, 1);
    res->app = app;
    res->root = g_strdup(root);
    app->worker = g_thread_new("sizer-scan", scan_worker, res);
}

/* ------------------------------------------------------------------ */
/* Chart drawing (pie / donut diagram)                                 */
/* ------------------------------------------------------------------ */

static gboolean chart_draw_cb(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    AppState *app = data;
    guint w = gtk_widget_get_allocated_width(widget);
    guint h = gtk_widget_get_allocated_height(widget);

    /* --- Cyber background: deep space with a faint neon grid. --- */
    cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, h);
    cairo_pattern_add_color_stop_rgb(bg, 0.0, 0.05, 0.08, 0.12);
    cairo_pattern_add_color_stop_rgb(bg, 1.0, 0.02, 0.03, 0.06);
    cairo_set_source(cr, bg);
    cairo_paint(cr);
    cairo_pattern_destroy(bg);

    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.90, 0.95, 0.06);
    for (guint gx = 0; gx < w; gx += 26) {
        cairo_move_to(cr, gx + 0.5, 0);
        cairo_line_to(cr, gx + 0.5, h);
    }
    for (guint gy = 0; gy < h; gy += 26) {
        cairo_move_to(cr, 0, gy + 0.5);
        cairo_line_to(cr, w, gy + 0.5);
    }
    cairo_stroke(cr);

    if (!app->entries || app->entries->len == 0 || app->total_size == 0) {
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 14);
        const char *msg = "[ AWAITING SCAN ]";
        cairo_text_extents_t ext;
        cairo_text_extents(cr, msg, &ext);
        cairo_set_source_rgba(cr, 0.0, 0.90, 0.95, 0.85);
        cairo_move_to(cr, (w - ext.width) / 2, h / 2);
        cairo_show_text(cr, msg);
        return FALSE;
    }

    gdouble cx = w / 2.0;
    gdouble cy = h / 2.0;
    gdouble radius = MIN(w, h) / 2.0 - 20.0;
    if (radius < 10) radius = 10;
    gdouble inner = radius * 0.58; /* donut hole */
    const gdouble gap = 0.012;     /* angular gap between slices (rad) */

    /* Outer neon ring framing the donut. */
    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgba(cr, 0.0, 0.90, 0.95, 0.25);
    cairo_arc(cr, cx, cy, radius + 6, 0, 2.0 * G_PI);
    cairo_stroke(cr);

    /* Draw the largest slices individually, group the remainder. */
    guint slices = MIN(app->entries->len, PALETTE_SIZE);
    gdouble start = -G_PI / 2.0; /* start at top */
    guint64 accounted = 0;

    for (guint i = 0; i <= slices; i++) {
        gdouble frac;
        const gdouble *col;
        gdouble grey[3] = {0.40, 0.45, 0.50};

        if (i < slices) {
            Entry *e = &g_array_index(app->entries, Entry, i);
            frac = (gdouble) e->size / (gdouble) app->total_size;
            col = palette[i % PALETTE_SIZE];
            accounted += e->size;
        } else {
            /* Remainder slice (everything not individually shown). */
            if (accounted >= app->total_size)
                break;
            frac = (gdouble)(app->total_size - accounted)
                   / (gdouble) app->total_size;
            col = grey;
        }

        gdouble end = start + frac * 2.0 * G_PI;
        gdouble a0 = start + gap / 2.0;
        gdouble a1 = end - gap / 2.0;
        if (a1 <= a0) { start = end; continue; }

        /* Glow underlay. */
        cairo_set_line_width(cr, radius - inner);
        cairo_set_source_rgba(cr, col[0], col[1], col[2], 0.18);
        cairo_arc(cr, cx, cy, (radius + inner) / 2.0, a0, a1);
        cairo_stroke(cr);

        /* Solid ring segment. */
        cairo_set_line_width(cr, (radius - inner) * 0.78);
        cairo_set_source_rgb(cr, col[0], col[1], col[2]);
        cairo_arc(cr, cx, cy, (radius + inner) / 2.0, a0, a1);
        cairo_stroke(cr);

        start = end;
    }

    /* Inner accent ring around the hole. */
    cairo_set_line_width(cr, 1.5);
    cairo_set_source_rgba(cr, 0.95, 0.20, 0.60, 0.55);
    cairo_arc(cr, cx, cy, inner - 3, 0, 2.0 * G_PI);
    cairo_stroke(cr);

    /* Center label: total size, neon cyan with a soft glow. */
    gchar *htotal = human_size(app->total_size);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 20);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, htotal, &ext);
    cairo_set_source_rgba(cr, 0.0, 0.90, 0.95, 0.30);
    cairo_move_to(cr, cx - ext.width / 2 + 1, cy + ext.height / 2 + 1);
    cairo_show_text(cr, htotal);
    cairo_set_source_rgb(cr, 0.85, 0.98, 1.0);
    cairo_move_to(cr, cx - ext.width / 2, cy + ext.height / 2);
    cairo_show_text(cr, htotal);
    g_free(htotal);

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    const char *sub = "TOTAL ON DISK";
    cairo_text_extents(cr, sub, &ext);
    cairo_set_source_rgba(cr, 0.0, 0.90, 0.95, 0.65);
    cairo_move_to(cr, cx - ext.width / 2, cy + 22);
    cairo_show_text(cr, sub);

    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Navigation & signal handlers                                        */
/* ------------------------------------------------------------------ */

static void on_scan_clicked(GtkButton *btn, gpointer data)
{
    (void) btn;
    AppState *app = data;
    const gchar *path = gtk_entry_get_text(GTK_ENTRY(app->path_entry));
    start_scan(app, path && *path ? path : "/");
}

static void on_stop_clicked(GtkButton *btn, gpointer data)
{
    (void) btn;
    AppState *app = data;
    if (app->scanning) {
        app->cancel_requested = TRUE;
        gtk_label_set_text(GTK_LABEL(app->status_label),
            "Cancelling...");
    }
}

static void on_up_clicked(GtkButton *btn, gpointer data)
{
    (void) btn;
    AppState *app = data;
    if (app->scanning || !app->current_root)
        return;
    if (g_strcmp0(app->current_root, "/") == 0)
        return;
    gchar *parent = g_path_get_dirname(app->current_root);
    start_scan(app, parent);
    g_free(parent);
}

static void on_root_clicked(GtkButton *btn, gpointer data)
{
    (void) btn;
    AppState *app = data;
    if (!app->scanning)
        start_scan(app, "/");
}

static void on_row_activated(GtkTreeView *tree, GtkTreePath *path,
                             GtkTreeViewColumn *col, gpointer data)
{
    (void) col;
    AppState *app = data;
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(model, &iter, path))
        return;
    gboolean is_dir = FALSE;
    gchar *fullpath = NULL;
    gtk_tree_model_get(model, &iter,
        COL_PATH, &fullpath, COL_IS_DIR, &is_dir, -1);
    if (is_dir && fullpath && !app->scanning)
        start_scan(app, fullpath);
    g_free(fullpath);
}

/* ------------------------------------------------------------------ */
/* About dialog                                                        */
/* ------------------------------------------------------------------ */

static void show_about(GtkWidget *parent)
{
    GtkAboutDialog *about = GTK_ABOUT_DIALOG(gtk_about_dialog_new());
    gtk_window_set_transient_for(GTK_WINDOW(about), GTK_WINDOW(parent));
    gtk_window_set_modal(GTK_WINDOW(about), TRUE);

    gtk_about_dialog_set_program_name(about, APP_NAME);
    gtk_about_dialog_set_version(about, "v" APP_VERSION);
    gtk_about_dialog_set_comments(about,
        "A disk space analyzer that reports the files and folders\n"
        "taking the most space on your system, with live percentages\n"
        "and an interactive donut diagram.");
    gtk_about_dialog_set_copyright(about,
        "\xC2\xA9 2026 " APP_AUTHOR);
    gtk_about_dialog_set_license_type(about, GTK_LICENSE_MIT_X11);

    const gchar *authors[] = { APP_AUTHOR, NULL };
    gtk_about_dialog_set_authors(about, authors);

    /* Use the installed/themed icon as the logo. */
    GdkPixbuf *logo = gtk_icon_theme_load_icon(
        gtk_icon_theme_get_default(), "sizer", 128,
        GTK_ICON_LOOKUP_USE_BUILTIN, NULL);
    if (logo) {
        gtk_about_dialog_set_logo(about, logo);
        g_object_unref(logo);
    } else {
        gtk_about_dialog_set_logo_icon_name(about, "sizer");
    }

    /* Feature list shown in a custom "Features" credits section. */
    const gchar *features[] = {
        "Full-system scan starting at /",
        "Per-item size and percentage of total",
        "In-cell percentage bars",
        "Interactive donut / pie diagram with color legend",
        "Double-click folders to drill down, Up / Root to navigate",
        "Threaded scanning with live progress and cancel",
        "Human-readable sizes (B / KB / MB / GB / TB)",
        NULL
    };
    gtk_about_dialog_add_credit_section(about, "Features", features);

    gtk_dialog_run(GTK_DIALOG(about));
    gtk_widget_destroy(GTK_WIDGET(about));
}

static void on_about_clicked(GtkButton *btn, gpointer data)
{
    (void) btn;
    AppState *app = data;
    show_about(app->window);
}

/* ------------------------------------------------------------------ */
/* UI construction                                                     */
/* ------------------------------------------------------------------ */

static void build_tree_columns(AppState *app)
{
    GtkCellRenderer *r;
    GtkTreeViewColumn *c;

    r = gtk_cell_renderer_text_new();
    c = gtk_tree_view_column_new_with_attributes("Name", r,
        "text", COL_NAME, NULL);
    gtk_tree_view_column_set_expand(c, TRUE);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_column_set_sort_column_id(c, COL_NAME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->tree), c);

    r = gtk_cell_renderer_text_new();
    c = gtk_tree_view_column_new_with_attributes("Type", r,
        "text", COL_TYPE, "foreground", COL_COLOR, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->tree), c);

    r = gtk_cell_renderer_text_new();
    g_object_set(r, "xalign", 1.0, NULL);
    c = gtk_tree_view_column_new_with_attributes("Size", r,
        "text", COL_SIZE_HUMAN, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->tree), c);

    r = gtk_cell_renderer_progress_new();
    c = gtk_tree_view_column_new_with_attributes("Share", r,
        "value", COL_BAR, "text", COL_PERCENT, NULL);
    gtk_tree_view_column_set_min_width(c, 160);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->tree), c);
}

static GtkWidget *make_toolbar(AppState *app)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(bar), 6);

    app->scan_button = gtk_button_new_with_label("Scan");
    gtk_button_set_image(GTK_BUTTON(app->scan_button),
        gtk_image_new_from_icon_name("system-search", GTK_ICON_SIZE_BUTTON));

    app->stop_button = gtk_button_new_with_label("Stop");
    gtk_button_set_image(GTK_BUTTON(app->stop_button),
        gtk_image_new_from_icon_name("process-stop", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_sensitive(app->stop_button, FALSE);

    app->up_button = gtk_button_new_with_label("Up");
    gtk_button_set_image(GTK_BUTTON(app->up_button),
        gtk_image_new_from_icon_name("go-up", GTK_ICON_SIZE_BUTTON));

    GtkWidget *root_button = gtk_button_new_with_label("Root /");
    gtk_button_set_image(GTK_BUTTON(root_button),
        gtk_image_new_from_icon_name("go-home", GTK_ICON_SIZE_BUTTON));

    app->path_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(app->path_entry), "/");
    gtk_widget_set_hexpand(app->path_entry, TRUE);

    GtkWidget *about_button = gtk_button_new_with_label("About");
    gtk_button_set_image(GTK_BUTTON(about_button),
        gtk_image_new_from_icon_name("help-about", GTK_ICON_SIZE_BUTTON));

    gtk_box_pack_start(GTK_BOX(bar), app->scan_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), app->stop_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), root_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), app->up_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), app->path_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bar), about_button, FALSE, FALSE, 0);

    g_signal_connect(app->scan_button, "clicked",
        G_CALLBACK(on_scan_clicked), app);
    g_signal_connect(app->stop_button, "clicked",
        G_CALLBACK(on_stop_clicked), app);
    g_signal_connect(app->up_button, "clicked",
        G_CALLBACK(on_up_clicked), app);
    g_signal_connect(root_button, "clicked",
        G_CALLBACK(on_root_clicked), app);
    g_signal_connect(about_button, "clicked",
        G_CALLBACK(on_about_clicked), app);
    g_signal_connect(app->path_entry, "activate",
        G_CALLBACK(on_scan_clicked), app);

    return bar;
}

static void activate(GtkApplication *gapp, gpointer data)
{
    AppState *app = data;

    apply_cyber_theme();

    app->window = gtk_application_window_new(gapp);
    gtk_window_set_title(GTK_WINDOW(app->window),
        APP_NAME " v" APP_VERSION " // Disk Space Analyzer");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 980, 620);
    gtk_window_set_position(GTK_WINDOW(app->window), GTK_WIN_POS_CENTER);

    /* Ensure the taskbar/window icon uses our installed themed icon. */
    gtk_window_set_icon_name(GTK_WINDOW(app->window), "sizer");
    {
        GdkPixbuf *icon = gtk_icon_theme_load_icon(
            gtk_icon_theme_get_default(), "sizer", 48,
            GTK_ICON_LOOKUP_USE_BUILTIN, NULL);
        if (icon) {
            gtk_window_set_icon(GTK_WINDOW(app->window), icon);
            g_object_unref(icon);
        }
    }

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), vbox);

    gtk_box_pack_start(GTK_BOX(vbox), make_toolbar(app), FALSE, FALSE, 0);

    /* Main paned area: list on the left, chart on the right. */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

    /* List. */
    app->store = gtk_list_store_new(N_COLUMNS,
        G_TYPE_STRING,  /* name        */
        G_TYPE_STRING,  /* size human  */
        G_TYPE_STRING,  /* percent     */
        G_TYPE_INT,     /* bar         */
        G_TYPE_STRING,  /* type        */
        G_TYPE_STRING,  /* path        */
        G_TYPE_BOOLEAN, /* is_dir      */
        G_TYPE_STRING); /* color       */
    app->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->store));
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(app->tree),
        GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    build_tree_columns(app);
    g_signal_connect(app->tree, "row-activated",
        G_CALLBACK(on_row_activated), app);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), app->tree);
    gtk_paned_pack1(GTK_PANED(paned), scroll, TRUE, FALSE);

    /* Right side: chart + total label. */
    GtkWidget *rbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(rbox), 6);
    GtkWidget *chart_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(chart_title),
        "<span foreground='#00e6f2' weight='bold' size='large'>"
        "&#9608; SPACE DISTRIBUTION</span>");
    gtk_box_pack_start(GTK_BOX(rbox), chart_title, FALSE, FALSE, 0);

    app->chart = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->chart, 320, 320);
    gtk_widget_set_vexpand(app->chart, TRUE);
    g_signal_connect(app->chart, "draw",
        G_CALLBACK(chart_draw_cb), app);
    gtk_box_pack_start(GTK_BOX(rbox), app->chart, TRUE, TRUE, 0);

    app->total_label = gtk_label_new("No scan yet.");
    gtk_box_pack_start(GTK_BOX(rbox), app->total_label, FALSE, FALSE, 0);
    gtk_paned_pack2(GTK_PANED(paned), rbox, FALSE, FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 600);

    /* Bottom: progress bar + status. */
    GtkWidget *statusbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(statusbox), 6);
    app->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(app->progress), TRUE);
    gtk_widget_set_hexpand(app->progress, TRUE);
    app->status_label = gtk_label_new("Ready. Click Scan to analyze /.");
    gtk_box_pack_start(GTK_BOX(statusbox), app->status_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(statusbox), app->progress, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), statusbox, FALSE, FALSE, 0);

    gtk_widget_show_all(app->window);

    /* If a path was supplied on the command line, scan it immediately. */
    if (app->initial_scan) {
        gtk_entry_set_text(GTK_ENTRY(app->path_entry), app->initial_scan);
        start_scan(app, app->initial_scan);
    }
}

int main(int argc, char **argv)
{
    AppState app;
    memset(&app, 0, sizeof(app));
    app.current_root = g_strdup("/");

    /* Optional command-line argument: a directory to scan on startup.
     * Defaults to scanning nothing (the user clicks Scan for "/").
     * Arguments are consumed here and not forwarded to GApplication. */
    if (argc > 1 && argv[1][0] != '-')
        app.initial_scan = g_strdup(argv[1]);

    GtkApplication *gapp = gtk_application_new(APP_ID,
        G_APPLICATION_NON_UNIQUE);
    g_signal_connect(gapp, "activate", G_CALLBACK(activate), &app);
    int status = g_application_run(G_APPLICATION(gapp), 0, NULL);

    if (app.entries) {
        for (guint i = 0; i < app.entries->len; i++)
            entry_clear(&g_array_index(app.entries, Entry, i));
        g_array_free(app.entries, TRUE);
    }
    g_free(app.current_root);
    g_free(app.initial_scan);
    g_object_unref(gapp);
    return status;
}
