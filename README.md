<div align="center">
  <img src="https://coconucos.cs.hhu.de/lehre/bigdata/resources/img/hhu-logo.svg" width=300>

  [![Download](https://img.shields.io/static/v1?label=&message=pdf&color=EE3F24&style=for-the-badge&logo=adobe-acrobat-reader&logoColor=FFFFFF)](/../-/jobs/artifacts/master/file/document/thesis.pdf?job=latex)
</div>

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
