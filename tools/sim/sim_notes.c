/*
 * Notas (F9.7) y explorador de archivos (F9.6) en la vista previa, con la
 * capa flexfs REAL sobre una carpeta del PC en lugar del LittleFS del reloj:
 * sin almacenamiento, nota nueva, escribir con el teclado (ñ, tildes,
 * Intro), autoguardado a los 2 s, vista previa leída del disco, opciones,
 * renombrar, papelera y restaurar, explorador con conteos reales, borrar
 * para siempre con doble toque, integridad visual y fugas.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "flexfs.h"
#include "sim.h"
#include "ui_internal.h"
#include "watch_storage.h"

static void click(lv_obj_t *o)
{
    if (o) {
        lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
    }
}

static bool has(const char *t)
{
    return sim_find_label(ui_nav_top(), t) != NULL;
}

static lv_obj_t *tap_of(const char *text)
{
    lv_obj_t *l = sim_find_label(ui_nav_top(), text);
    while (l && !lv_obj_has_flag(l, LV_OBJ_FLAG_CLICKABLE)) {
        l = lv_obj_get_parent(l);
    }
    return l;
}

static lv_obj_t *find_class(lv_obj_t *o, const lv_obj_class_t *cls)
{
    if (lv_obj_check_type(o, cls)) {
        return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = find_class(lv_obj_get_child(o, (int32_t)i), cls);
        if (r) {
            return r;
        }
    }
    return NULL;
}

static bool key(const char *t)
{
    lv_obj_t *kb = find_class(ui_nav_top(), &lv_buttonmatrix_class);
    if (!kb) {
        return false;
    }
    const char *const *map = lv_buttonmatrix_get_map(kb);
    for (uint32_t i = 0, b = 0; map[i][0]; i++) {
        if (strcmp(map[i], "\n") == 0) {
            continue;
        }
        if (strcmp(map[i], t) == 0) {
            lv_buttonmatrix_set_selected_button(kb, b);
            lv_obj_send_event(kb, LV_EVENT_VALUE_CHANGED, NULL);
            sim_run(30);
            return true;
        }
        b++;
    }
    return false;
}

void sim_suite_notes(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_run(400);
    const watch_storage_info_t saved = sim_stg;

    /* Sin almacenamiento montado: se dice por qué, no una lista vacía */
    ui_nav_push(ui_notes_create);
    sim_run(400);
    EXPECT(has("Notas sin almacenamiento"), "notas: no explica que falta el almacenamiento");
    sim_lint("notas sin almacenamiento");
    sim_save(out, "f9_notas_sin_almacenamiento");
    ui_nav_back();
    sim_run(400);

    /* Almacenamiento montado sobre una carpeta limpia del PC */
    char root[400], cmd[460];
    snprintf(root, sizeof(root), "%s/fs_notas", out);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    EXPECT(system(cmd) == 0, "no se pudo limpiar %s", root);
    mkdir(root, 0775);
    ffs_set_root(root);
    sim_stg.state = STG_MOUNTED;
    sim_stg.gate = STG_GATE_OK;
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    ui_nav_push(ui_notes_create);
    sim_run(400);
    EXPECT(has("Nueva nota") && has("No hay notas todavía"), "notas: lista vacía incompleta");
    EXPECT(ffs_is_dir("/Notas") && ffs_is_dir("/Papelera"), "notas: no crea las carpetas fijas");

    /* Nota nueva: "Sin título 1", teclado con ñ, tildes e Intro */
    click(tap_of("Nueva nota"));
    sim_run(500);
    EXPECT(has("Sin título 1") && has("Guardado"), "nota nueva: no abre el editor");
    EXPECT(ffs_exists("/Notas/Sin título 1.txt"), "nota nueva: no existe en el disco");
    key("ABC");
    key("H");
    key("o");
    key("l");
    key("a");
    key("Espacio");
    key("ñ");
    key("Intro");
    key("áé");
    key("á");
    key("¿");
    key("abc");
    EXPECT(has("Sin guardar"), "editor: no avisa de cambios sin guardar");
    sim_run(2600);
    char t[FFS_TEXT_MAX];
    ffs_read_text("/Notas/Sin título 1.txt", t, sizeof(t));
    EXPECT(strcmp(t, "Hola ñ\ná¿") == 0 && has("Guardado"), "autoguardado: en el disco hay \"%s\"", t);
    sim_lint("nota editor");
    sim_save(out, "f9_nota_editor");
    key("x"); /* lo último se guarda al salir aunque no hayan pasado 2 s */
    ui_nav_back();
    sim_run(1200); /* animación de vuelta + siguiente tic de la lista */
    ffs_read_text("/Notas/Sin título 1.txt", t, sizeof(t));
    EXPECT(strcmp(t, "Hola ñ\ná¿x") == 0, "al salir no se guarda: \"%s\"", t);
    EXPECT(has("Sin título 1") && has("Hola ñ á¿x"), "lista: sin título o vista previa real");
    sim_lint("notas");
    sim_save(out, "f9_notas");

    /* Opciones (mantener pulsado) y renombrar */
    lv_obj_t *card = tap_of("Sin título 1");
    if (card) {
        lv_obj_send_event(card, LV_EVENT_LONG_PRESSED, NULL);
    }
    sim_run(500);
    EXPECT(has("Abrir") && has("Renombrar") && has("Mover a la papelera") && has("Borrar definitivamente"),
           "opciones incompletas");
    sim_lint("opciones de nota");
    sim_save(out, "f9_nota_opciones");
    click(tap_of("Renombrar"));
    sim_run(500);
    lv_obj_t *ta = find_class(ui_nav_top(), &lv_textarea_class);
    EXPECT(ta && strcmp(lv_textarea_get_text(ta), "Sin título 1") == 0, "renombrar: no propone el nombre");
    if (ta) {
        lv_textarea_set_text(ta, "a/b");
    }
    key("OK");
    EXPECT(has("Nombre no válido: sin / ni @, ni espacios al principio o al final"), "renombrar: acepta '/'");
    if (ta) {
        lv_textarea_set_text(ta, "Compra");
    }
    key("OK");
    sim_run(1500); /* vuelve y las opciones se cierran solas */
    EXPECT(ffs_exists("/Notas/Compra.txt") && !ffs_exists("/Notas/Sin título 1.txt"), "renombrar: no en el disco");
    EXPECT(has("Compra") && has("Nueva nota"), "renombrar: no vuelve a la lista con el nombre nuevo");

    /* Papelera: ir y volver */
    card = tap_of("Compra");
    if (card) {
        lv_obj_send_event(card, LV_EVENT_LONG_PRESSED, NULL);
    }
    sim_run(500);
    click(tap_of("Mover a la papelera"));
    sim_run(1000);
    EXPECT(has("No hay notas todavía") && ffs_exists("/Papelera/@Notas@Compra.txt"), "papelera: no se movió");
    ui_files_open_dir("/Papelera");
    sim_run(500);
    EXPECT(has("/Notas/Compra.txt") && has("Vaciar la papelera"), "papelera: no dice de dónde vino");
    sim_lint("papelera");
    sim_save(out, "f9_papelera");
    click(tap_of("/Notas/Compra.txt"));
    sim_run(500);
    EXPECT(has("Restaurar") && has("Estaba en /Notas/Compra.txt"), "papelera: opciones sin restaurar");
    click(tap_of("Restaurar"));
    sim_run(1000);
    EXPECT(has("Carpeta vacía") && ffs_exists("/Notas/Compra.txt"), "restaurar: no vuelve a su sitio");
    ui_nav_back();
    sim_run(500);
    ui_nav_back();
    sim_run(500);

    /* Explorador: conteos reales y abrir una nota desde él */
    ffs_write_text("/Documentos/plan.bin", "1234567890");
    ui_files_open_dir("/");
    sim_run(500);
    EXPECT(has("Documentos") && has("Notas") && has("Papelera") && has("1 elemento") && has("0 elementos"),
           "explorador: sin carpetas o conteos");
    sim_lint("archivos");
    sim_save(out, "f9_archivos");
    click(tap_of("Documentos"));
    sim_run(500);
    EXPECT(has("plan.bin") && has("10 B"), "explorador: no lista el archivo con su tamaño");
    click(tap_of("plan.bin"));
    sim_run(500);
    EXPECT(has("El reloj no abre este tipo de archivo"), "explorador: no dice que no lo abre");
    click(tap_of("Borrar definitivamente"));
    sim_run(100);
    EXPECT(has("Toca otra vez para borrar") && ffs_exists("/Documentos/plan.bin"), "borrar: sin confirmación");
    click(tap_of("Toca otra vez para borrar"));
    sim_run(1000);
    EXPECT(!ffs_exists("/Documentos/plan.bin") && has("Carpeta vacía"), "borrar: sigue en el disco");
    ui_nav_back();
    sim_run(500);
    click(tap_of("Notas"));
    sim_run(500);
    click(tap_of("Compra.txt"));
    sim_run(500);
    ta = find_class(ui_nav_top(), &lv_textarea_class);
    EXPECT(ta && strcmp(lv_textarea_get_text(ta), "Hola ñ\ná¿x") == 0, "explorador: el .txt no abre en Notas");

    ui_nav_home();
    sim_run(1200);
    EXPECT(sim_mem_blocks() == blocks0, "fuga en Notas/Archivos: %u -> %u bloques", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    sim_stg = saved;
    ffs_set_root(NULL);
    EXPECT(system(cmd) == 0, "no se pudo limpiar %s", root);
    printf("notas y archivos F9.6/F9.7: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
