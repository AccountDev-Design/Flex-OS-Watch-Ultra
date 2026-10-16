/*
 * Pruebas de watch_files: nombres y rutas (files_logic) y la capa de
 * archivos (flexfs) sobre una carpeta temporal real del PC, con la
 * semántica de FlexOS_FS de la referencia.
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "files_logic.h"
#include "flexfs.h"
#include "test_util.h"

static void test_logic(void)
{
    char b[FL_PATH_MAX];
    CHECK(strcmp(fl_base("/Notas/Lista.txt"), "Lista.txt") == 0);
    CHECK(strcmp(fl_ext("Lista.txt"), ".txt") == 0 && strcmp(fl_ext(".oculto"), "") == 0);
    CHECK(strcmp(fl_ext("/Notas.d/sin"), "") == 0);
    fl_stem("/Notas/Lista de la compra.txt", b, sizeof(b));
    CHECK(strcmp(b, "Lista de la compra") == 0);
    fl_parent("/Notas/a.txt", b, sizeof(b));
    CHECK(strcmp(b, "/Notas") == 0);
    fl_parent("/a", b, sizeof(b));
    CHECK(strcmp(b, "/") == 0);
    CHECK(fl_join("/", "Notas", b, sizeof(b)) && strcmp(b, "/Notas") == 0);
    CHECK(fl_join("/Notas", "a.txt", b, sizeof(b)) && strcmp(b, "/Notas/a.txt") == 0);
    char small[8];
    CHECK(!fl_join("/Notas", "largo.txt", small, sizeof(small)));

    CHECK(fl_name_ok("Lista 1.txt") && fl_name_ok("Año"));
    CHECK(!fl_name_ok("") && !fl_name_ok("a/b") && !fl_name_ok("a@b") && !fl_name_ok(" a") && !fl_name_ok("a "));
    CHECK(!fl_name_ok("..") && !fl_name_ok("a\nb"));
    char lng[FL_NAME_MAX + 4];
    memset(lng, 'x', sizeof(lng) - 1);
    lng[sizeof(lng) - 1] = 0;
    CHECK(!fl_name_ok(lng));

    /* Papelera: la ruta va en el nombre (FS.cpp:451-469) */
    CHECK(fl_trash_encode("/Notas/Sin título 1.txt", b, sizeof(b)) && strcmp(b, "@Notas@Sin título 1.txt") == 0);
    CHECK(fl_trash_origin(b, small, sizeof(small)) == false); /* no cabe */
    char o[FL_PATH_MAX];
    CHECK(fl_trash_origin("@Notas@Sin título 1.txt", o, sizeof(o)) && strcmp(o, "/Notas/Sin título 1.txt") == 0);
    CHECK(fl_trash_origin("@Notas@a.txt@@2", o, sizeof(o)) && strcmp(o, "/Notas/a.txt") == 0); /* RELOJ */
    CHECK(fl_trash_origin("@Docs@b (3)", o, sizeof(o)) && strcmp(o, "/Docs/b (3)") == 0);
    CHECK(fl_trash_origin("@Docs@foto(2)", o, sizeof(o)) && strcmp(o, "/Docs/foto(2)") == 0);
    CHECK(!fl_trash_origin("suelto.txt", o, sizeof(o)));
    CHECK(!fl_trash_encode("relativa", b, sizeof(b)));

    fl_fmt_size(512, b, sizeof(b));
    CHECK(strcmp(b, "512 B") == 0);
    fl_fmt_size(12 * 1024 + 100, b, sizeof(b));
    CHECK(strcmp(b, "12 KB") == 0);
    fl_fmt_size(3 * 1048576 + 524288, b, sizeof(b));
    CHECK(strcmp(b, "3,5 MB") == 0);
    char u1[] = "caf\xC3", u2[] = "café", u3[] = "a\xE2\x82", u4[] = "ab";
    fl_utf8_trim(u1);
    fl_utf8_trim(u2);
    fl_utf8_trim(u3);
    fl_utf8_trim(u4);
    CHECK(strcmp(u1, "caf") == 0 && strcmp(u2, "café") == 0 && strcmp(u3, "a") == 0 && strcmp(u4, "ab") == 0);
    CHECK(fl_cmp(true, "z", false, "a") < 0 && fl_cmp(false, "B", false, "a") > 0 && fl_cmp(false, "a", false, "A") == 0);
}

static void rm_rf(const char *p)
{
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", p);
    CHECK(system(cmd) == 0);
}

static void test_fs(void)
{
    const char *tmp = getenv("TMPDIR");
    char root[200];
    snprintf(root, sizeof(root), "%s/flexfs_XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
    CHECK(mkdtemp(root) != NULL);
    ffs_set_root("/no/existe");
    CHECK(!ffs_ready()); /* sin montar no hay nada */
    ffs_set_root(root);
    CHECK(ffs_ready());
    ffs_init_dirs();
    CHECK(ffs_is_dir("/Notas") && ffs_is_dir("/Papelera") && ffs_is_dir("/Documentos") && ffs_is_dir("/Paint"));
    CHECK(!ffs_exists("/../etc/passwd")); /* nada fuera de la raíz */

    /* Nombres nuevos "Sin título N" */
    char p1[FL_PATH_MAX], p2[FL_PATH_MAX], np[FL_PATH_MAX];
    CHECK(ffs_new_name("/Notas", "Sin título", ".txt", p1, sizeof(p1)) && strcmp(p1, "/Notas/Sin título 1.txt") == 0);
    CHECK(ffs_write_text(p1, "Comprar pan\nLeche"));
    CHECK(ffs_new_name("/Notas", "Sin título", ".txt", p2, sizeof(p2)) && strcmp(p2, "/Notas/Sin título 2.txt") == 0);
    CHECK(ffs_write_text(p2, ""));
    char t[FFS_TEXT_MAX];
    CHECK(ffs_read_text(p1, t, sizeof(t)) == 17 && strcmp(t, "Comprar pan\nLeche") == 0);
    CHECK(ffs_read_text(p2, t, sizeof(t)) == 0 && t[0] == 0);
    CHECK(ffs_read_text("/Notas/no.txt", t, sizeof(t)) == -1);
    char tiny[5];
    CHECK(ffs_read_text(p1, tiny, sizeof(tiny)) == 4 && strcmp(tiny, "Comp") == 0);
    /* Reescribir es atómico y no deja temporales a la vista */
    CHECK(ffs_write_text(p1, "Comprar pan"));
    CHECK(ffs_read_text(p1, t, sizeof(t)) == 11);

    /* Lista: carpetas primero, conteo real */
    ffs_entry_t e[FFS_LIST_MAX];
    int n = ffs_list("/", e, FFS_LIST_MAX);
    CHECK_EQ(n, 4);
    CHECK(n == 4 && e[0].dir && strcmp(e[0].name, "Documentos") == 0 && strcmp(e[1].name, "Notas") == 0);
    CHECK(n == 4 && e[1].items == 2 && e[1].size == 11);
    CHECK(ffs_list("/nada", e, FFS_LIST_MAX) < 0);
    CHECK_EQ(ffs_count("/Notas"), 2);

    /* Renombrar: conserva la extensión y no pisa a otro */
    CHECK(ffs_rename(p1, "Compra", np, sizeof(np)) && strcmp(np, "/Notas/Compra.txt") == 0);
    CHECK(!ffs_rename(np, "Sin título 2", NULL, 0)); /* ya existe */
    CHECK(!ffs_rename(np, "a/b", NULL, 0) && !ffs_rename(np, "a@b", NULL, 0));
    CHECK(ffs_rename(np, "Compra", NULL, 0)); /* mismo nombre: nada que hacer */

    /* Papelera: ir, colisión numerada, restaurar a su sitio */
    CHECK(ffs_trash(np) && !ffs_exists(np) && ffs_exists("/Papelera/@Notas@Compra.txt"));
    CHECK(ffs_write_text("/Notas/Compra.txt", "otra"));
    CHECK(ffs_trash("/Notas/Compra.txt") && ffs_exists("/Papelera/@Notas@Compra.txt@@2"));
    CHECK(!ffs_trash("/Papelera/@Notas@Compra.txt")); /* la papelera no se tira */
    CHECK(ffs_restore("@Notas@Compra.txt") && ffs_exists("/Notas/Compra.txt"));
    CHECK(ffs_restore("@Notas@Compra.txt@@2") && ffs_exists("/Notas/Compra (2).txt"));
    CHECK(ffs_read_text("/Notas/Compra (2).txt", t, sizeof(t)) == 4);
    /* La carpeta de origen ya no existe: se recrea al restaurar */
    CHECK(ffs_mkdir("/Documentos/Viaje") && ffs_write_text("/Documentos/Viaje/plan.txt", "x"));
    CHECK(ffs_trash("/Documentos/Viaje/plan.txt") && ffs_delete("/Documentos/Viaje"));
    CHECK(ffs_restore("@Documentos@Viaje@plan.txt") && ffs_exists("/Documentos/Viaje/plan.txt"));

    /* Borrar una carpeta con contenido y vaciar la papelera */
    CHECK(ffs_trash("/Documentos/Viaje"));
    CHECK(ffs_trash("/Notas/Compra (2).txt"));
    CHECK_EQ(ffs_count("/Papelera"), 2);
    CHECK(ffs_empty_trash() && ffs_count("/Papelera") == 0);
    CHECK(!ffs_delete("/"));
    CHECK(ffs_delete("/Notas/Sin título 2.txt") && ffs_count("/Notas") == 1);

    rm_rf(root);
    ffs_set_root(NULL);
}

void test_files_all(void)
{
    test_logic();
    test_fs();
}
