# SDD ledger — plan: docs/refactor-plan.md

## Mediu și finalizarea etapei 3 — 2026-11-09

Verificat: `Raspberry Pi test host`, `aarch64`, Raspberry Pi 5 Model B Rev 1.1, directorul
`<project-root>`. Modificările inițiale, inclusiv fișierele noi,
aparțineau exclusiv etapei 3; backupul existent era separat și a rămas necomis.

Observația despre `created` a fost verificată: nu exista un defect de producție.
FULL și ID_EXHAUSTED ies înainte de atribuirea outputului. Testele C au fost
întărite cu un pointer sentinel pentru fiecare scenariu de eșec exercitat,
inclusiv FULL combinat cu epuizarea ID-urilor. Concluzia și verificările au fost
adăugate în raportul etapei 3 înainte de commit. Nu s-a schimbat implementarea.

Build-urile curate cu Clang și GCC, folosind explicit același CC și la `make
test`, au trecut fiecare 17 teste Python, 8 grupuri HTTP C și 6 grupuri storage C.
Nu au existat diagnostice; `git diff --check` curat.

Commit local: `c16ee14`, `refactor: extract task storage module`, exclusiv
Makefile, main.c, task_storage.c/h, test_api.py, test_task_storage.c și raportul
stage-3-results.md. Nu s-a folosit `git add -A`; backups/ nu este urmărit de Git.

## Etapa 4 — responsabilități și dependențe

S-au extras din main.c în `http_parser.c/h`:

- struct Request și limitele HTTP existente;
- parsarea request-line cu lățimi sincronizate cu bufferele;
- parsarea headerelor și conversia Content-Length cu verificarea overflow-ului;
- colectarea limitată a octeților și identificarea separatorului header/body;
- copierea body-ului, respingerea NUL și terminarea explicită a șirului;
- recepția parțială, reluarea după EINTR și maparea EOF/erorilor la statusul existent.

Interfața este cea planificată:
`int http_read_request(int fd, struct Request *req)` — 0 sau status HTTP.
Apelantul inițializează Request la zero. La eroare, câmpurile parțial parsate nu
se folosesc. Parserul împrumută fd-ul, fără să-l închidă sau să-i seteze timeoutul.
Contextul local al cititorului deține bufferul temporar; body-ul este copiat în
Request înainte de întoarcere. Nu se păstrează pointeri spre bufferul temporar.

Dependențe: bibliotecă C, POSIX recv/strtok_r/strcasecmp și constantele de status
existente din http_response.h. Parserul nu trimite răspunsuri și nu accesează
routerul, JSON-ul sau storage. Nu s-au instalat dependențe.

Riscuri identificate înaintea implementării: ordinea primei erori raportate,
overflow numeric, sincronizarea capacităților cu sscanf, separator fragmentat,
EOF prematur, NUL în body, limitele exacte de header/body și ownership-ul fd-ului.
S-au păstrat header 8192, body 4096, method 15 și path 159 octeți utili, precum
și dimensiunea bufferului wire original. Lățimile sscanf sunt construite din
aceleași macro-uri folosite pentru capacități, fără valori duplicate în format.

## Fișierele etapei 4

| Fișier | Modificare |
| --- | --- |
| `src/http_parser.h` | Limite HTTP, Request și contractul funcției publice. |
| `src/http_parser.c` | Helpers pentru request-line, un header, setul de headere, recepție și body; orchestrarea citirii. |
| `src/main.c` | Eliminarea parserului/Request/limitelor mutate, includerea headerului și apelul http_read_request. |
| `Makefile` | Sursa și headerul noi; harness C parser în make test. |
| `tests/test_api.py` | Extinderea helperului raw pentru chunks și shutdown; opt teste noi de caracterizare. |
| `tests/test_http_parser.c` | Șase grupuri C cu recv controlat, fără hooks în producție. |
| `docs/stage-4-results.md` | Acest raport. |

Comparația automatizată cu HEAD confirmă că main.c diferă numai prin extragerea
parserului, includerea headerului și redenumirea apelului. Storage, HTTP response,
testele lor C și dashboardul sunt identice octet cu octet cu versiunea comisă.
Nu s-a schimbat contractul endpointurilor, JSON-ul, MIME sau headerele răspunsurilor.

## Teste înainte și după refactorizare

Înainte de schimbarea implementării, `make test` pe parserul original a trecut
**25/25 metode Python**, plus 8 grupuri HTTP C și 6 grupuri storage C. Cele opt
metode Python noi verifică:

1. Content-Length duplicat, negativ, text, gol și overflow de size_t pe ARM64 ->400.
2. Transfer-Encoding ->400.
3. Lungime declarată 4097 ->413 și body valid de exact 4096 octeți ->201.
4. Header fără terminator de 8192 octeți ->413.
5. Header complet de 8192 octeți ->200 și 8193 ->413.
6. Cerere trimisă în fragmente, inclusiv separatorul și body-ul ->201.
7. Body cu NUL ->400.
8. Body incomplet și shutdown(SHUT_WR) ->400.

Testul existent pentru POST fără Content-Length continuă să verifice 411.
Trimiterile TCP multiple pot fi comasate de kernel; de aceea harness-ul C
suplimentar forțează recepția de un octet și EINTR. Cele șase grupuri verifică
fragmentare/EINTR, EOF în body/header/cerere goală, EIO, limite method/path,
request-line invalid și prioritatea erorilor în headere. Nu se presupune că
limitele apelurilor send sunt limite de pachete sau recv.

După extragere, build-ul Clang și suita au trecut, apoi au fost adăugate și rulate
testele deterministe C ale parserului. Verificarea finală cerută:

- `make clean && make CC=clang && make test`: **25/25 Python**, **8 grupuri HTTP C**,
  **6 storage C**, **6 parser C**, toate OK, fără diagnostice.
- Deoarece make test fără CC folosește implicit GCC pentru harness-uri, s-a
  executat suplimentar `make CC=clang -B build/test_http_response
  build/test_task_storage build/test_http_parser`, apoi fiecare binar: toate
  cele **20 de grupuri C** au trecut și cu Clang.
- `make clean && make CC=gcc && make test`: aceleași **25/25 Python + 20 grupuri C**,
  toate OK, fără diagnostice. Artefactele finale din build sunt GCC.
- `git diff --check`: fără erori.

Cele 25 sunt metode unittest; subtestele și aserțiunile nu sunt numărate ca teste
separate. Cele 20 sunt grupuri în trei harness-uri C, nu metode Python.
Testele de integrare au fost executate autorizat în afara sandboxului pentru
socketuri localhost; harness-urile C nu necesită socketuri reale.

Task 4: complete (base c16ee14, changes uncommitted; Clang/GCC: Python 25/25,
HTTP C 8 groups, storage C 6 groups, parser C 6 groups; diff check clean).

## Probleme, review și limitări

Nu au apărut eșecuri noi de test sau defecte care să necesite o schimbare de
comportament. Extragerea a păstrat suita verde; nu s-a fabricat un RED pentru
refactorizarea structurală. RED → GREEN → REFACTOR rămâne cerut pentru orice
schimbare de comportament viitoare.

Review independent read-only prin requesting-code-review: fără probleme critice,
importante sau minore identificate. Reviewerul a verificat sursele, comparația
cu implementarea veche și testele, dar nu a rerulat suita de integrare și nu a
revizuit acest raport, redactat după verdict. Rezultatele de execuție sunt ale
agentului principal. Nu s-a folosit un instrument de complexitate ciclomatică;
nu se inventează scoruri. Helpers au responsabilități distincte și ramuri simple.

- Ruling: se păstrează tokenizerul tolerant CR/LF, ignorarea octeților suplimentari
  și ordinea erorilor existente. Cost: parserul rămâne un subset HTTP educațional;
  întărirea validării necesită teste și o decizie separată.
- Ruling: routerul, JSON-ul, ciclul de viață al serverului și constantele lor
  existente rămân în etapele ulterioare. Cost: limitările acelor componente nu
  sunt remediate în această extragere.
- Ruling: se lucrează în checkoutul și ramura existente, cu acest raport ca ledger,
  fără worktree nou, ștergeri sau commit al etapei 4. Cost: modificările rămân
  vizibile și necomise în checkoutul curent.
- recv este interceptat la link numai în harness, cu opțiunea GNU --wrap deja
  folosită de infrastructura testelor HTTP. EINTR/EIO sunt simulate determinist;
  nu se revendică o probă a semnalelor reale sau a tuturor segmentărilor TCP.
- Rămân limitările existente: validare Unicode directă incompletă în JSON,
  posibil reset la upload respins cu octeți necitiți, erori send fără propagare.
  Nu s-au schimbat aceste politici și nu s-au identificat defecte noi de securitate.

## Recomandări pentru etapa 5

Urmează exclusiv routerul și helpers JSON conform planului: caracterizare pentru
titluri de 120/121 octeți, limite multioctet, surrogate pairs valide/invalide,
control escaped, câmpuri suplimentare și JSON trailing; apoi extragere cu
serializerul comun păstrat. Centralizarea rutelor și înlocuirea lungimii prefixului
DELETE 11 aparțin acelei etape. Etapa 5 nu a fost începută.

## Starea finală

```text
 M Makefile
 M src/main.c
 M tests/test_api.py
?? backups/
?? docs/stage-4-results.md
?? src/http_parser.c
?? src/http_parser.h
?? tests/test_http_parser.c
```

Index gol. backups/ rămâne necomis. Etapa 4 nu este comisă; niciun push.

## Pregătirea commitului etapei 4

Verificarea suplimentară a `docs/stage-3-results.md` nu a găsit modificări
necomise: explicația și testele pentru `created` erau deja incluse legitim în
commitul `c16ee14`. Nu se include din nou fișierul într-un commit ulterior.
S-a adăugat `/backups/` în .gitignore la cererea utilizatorului, pentru a păstra
backupurile existente local și a preveni includerea lor accidentală în Git.
