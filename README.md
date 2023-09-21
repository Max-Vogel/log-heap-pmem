<div align="center">
  <img src="https://coconucos.cs.hhu.de/lehre/bigdata/resources/img/hhu-logo.svg" width=300>

  [![Download](https://img.shields.io/static/v1?label=&message=pdf&color=EE3F24&style=for-the-badge&logo=adobe-acrobat-reader&logoColor=FFFFFF)](/../-/jobs/artifacts/master/file/document/thesis.pdf?job=latex)
</div>

# Thesis PDF

Weil die Pipeline fehlschlägt, befindet sich die fertige Thesis als PDF im `document` Verzeichnis.


# :notebook: &nbsp; Aufgabenbeschreibung

- Implementierung eines Log strukturierten persistenten Speichers
- Logstruktur: Wenn Objekte neu erstellt oder verändert werden, werden sie an das Ende des Logs angehangen
- Hashtabelle zum verwalten der Objekte liegt im RAM
- Objekte benötigen ID und Versionsnummer (bzw. etwas um die aktuellste Version zu erkennen)
- Bei Absturz des Systems wird die Hashtabelle neu aufgebaut, indem der persistente Speicher durchgegangen wird
- Log in Segmente unterteilt (wie bei RAMCloud)
- Cleaner entfernt alte Versionen von Objekten und gelöschte Objekte
- Cleaner kompaktiert den Speicher, indem Objekte aus Segmenten mit vielen leeren Stellen in neue Segmente kopiert werden und das alte Segment freigegeben wird
- Die Komplexität des Cleaners wird angepasst, je nachdem wie gut und schnell ich voran komme
- Die Performance wird mithilfe von YCSB getestet

# Programm bauen und verwenden

Um den Log-strukturierten Heap verwenden zu können werden die Bibliotheken libpmem2 und GLib benötigt. Diese müssen auf dem System installiert sein.
Die zum kompilieren verwendete C Version ist `gnu17`.

Wenn man ihn in anderen Programmen verwenden möchte, kann im projects Verzeichnis der Befehl `make static_lib` verewendet werden, um eine statische Bibliothek zu bauen.
Diese muss beim Kompilieren des eigenen Programms eingebunden werden. Das geschieht mit den Flags `-llog_pmem` und `-L/path/to/project/`.
Zusätzlich muss `-lpmem2` angegeben werden und \``pkg-config --cflags glib-2.0`\` sowie \``pkg-config --libs glib-2.0`\`. Die beiden pkg-config Befehle müssen jeweils mit Backticks umschlossen werden.
Im Programm selbst muss die Headerdatei `log_pmem.h` included werden.
Ein beispielhafter Aufruf ist:

```
gcc main.c -o main -std=gnu17 -llog_pmem -lpmem2 -L/path/to/project/ `pkg-config --cflags glib-2.0` `pkg-config --libs glib-2.0`
```

Alternativ kann für simples und schnelles ausprobieren die Datei `main.c` verwendet werden. 
Hier können direkt die gewünschten Funktionen aufgerufen werden und das Programm mit `make` gebaut werden.

Um die Tests zu bauen müssen die Befehle `make test` und `make test_big` ausgeführt werden.

Um den Benchmark zu bauen muss zuerst die statische Bibliothek mit `make static_lib` gebaut werden.
Anschließend muss in das Verzeichnis `benchmark/ycsb-storedsbench-master` gewechselt werden und `make` ausgeführt werden.
Die erstellten Skripte zum ausführen der Benchmarks befinden sich im Verzeichnis `benchmark/ycsb-storedsbench-master/scripts/log_pmem`.
Die Ergebnisse, der von mir ausgeführen Benchmarks, sind im Verzeichnis `benchmark/ergebnisse`

Um den Benchmark oder das Programm auf dem Cluster laufen zu lassen, muss aus der Datei `pmem.def` in Verzeichnis `benchmark` der passende Singularity Container gebaut werden.