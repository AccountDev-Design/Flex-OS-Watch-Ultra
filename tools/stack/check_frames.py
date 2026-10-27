#!/usr/bin/env python3
"""Marcos de pila del firmware medidos por el compilador Xtensa.

Compila con:   idf.py -DFLEX_STACK_USAGE=1 build     (dentro de "Flex OS Ultra Watch")
Comprueba con: python3 tools/stack/check_frames.py "Flex OS Ultra Watch/build" [--limit 3072]

GCC escribe un .su por archivo con el tamaño del marco de cada función PARA EL
CHIP (no los del PC: un hilo de PC tiene 8 MB y ahí nada desborda). Falla si
una función de components/ o main/ del proyecto pasa del límite: un array
grande en la pila de una tarea pequeña reinicia el reloj (la Galería, con
8224 B en gal__origin_load dentro de 12 KB, docs/GALERIA.md §13).
"""
import os
import sys


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    limit = 3072
    if "--limit" in sys.argv:
        limit = int(sys.argv[sys.argv.index("--limit") + 1])
        args = [a for a in args if a != str(limit)]
    if not args:
        print(__doc__)
        return 2
    build = args[0]
    rows = []
    for root, _dirs, files in os.walk(build):
        for f in files:
            if not f.endswith(".su"):
                continue
            with open(os.path.join(root, f), encoding="utf-8", errors="replace") as fh:
                for line in fh:
                    parts = line.rstrip("\n").split("\t")
                    if len(parts) < 2:
                        continue
                    where, size = parts[0], int(parts[1])
                    ours = ("/components/" in where or "/main/" in where) and "Flex OS Ultra Watch" in where
                    if ours and "/managed_components/" not in where:
                        rows.append((size, where, parts[2] if len(parts) > 2 else ""))
    if not rows:
        print("no hay archivos .su: compila con -DFLEX_STACK_USAGE=1")
        return 2
    rows.sort(reverse=True)
    print("Marcos de pila más grandes del proyecto (Xtensa):")
    for size, where, kind in rows[:15]:
        print(f"  {size:6d} B  {where.split('Flex OS Ultra Watch/')[-1]}  {kind}")
    bad = [r for r in rows if r[0] > limit]
    if bad:
        print(f"FALLO: {len(bad)} funciones con un marco de más de {limit} B")
        return 1
    print(f"OK: ninguna función del proyecto pasa de {limit} B ({len(rows)} medidas)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
