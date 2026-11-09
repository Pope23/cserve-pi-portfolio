# SDD ledger — plan: docs/refactor-plan.md

## Mediu și finalizarea etapei 2 — 2026-11-09

Host verificat: `Raspberry Pi test host`, `aarch64`, Raspberry Pi 5 Model B Rev 1.1.
Director: `<project-root>`.

`git status` și `git diff` inițiale au confirmat exclusiv fișierele etapei 2:
Makefile, main.c, http_response.c/h, test_api.py, test_http_response.c și raportul
stage-2-results.md. Directorul `backups/` era deja prezent și a rămas exclus.
Indexul era gol; `git diff --check` nu a raportat erori.

Înainte de commit au trecut:

- `make clean && make CC=clang && make test`: 16/16 teste de integrare, 8 cazuri C.
- `make CC=clang -B build/test_http_response && ./build/test_http_response`:
  8 cazuri C, compilate explicit cu Clang.
- `make clean && make CC=gcc && make test`: 16/16 teste de integrare, 8 cazuri C.

Comanda `make test` fără CC folosește GCC implicit; de aceea harness-ul HTTP a
fost verificat separat cu Clang. Niciun diagnostic de compilare.

Commit local: `f1a1449`, `refactor: extract HTTP response module`.
Conține exclusiv cele șapte fișiere ale etapei 2. Niciun push.
Raportul etapei 2 rămâne fotografia situației de dinaintea acestui commit.

## Etapa 3 — responsabilități, interfețe și riscuri

Înainte de modificarea codului s-au identificat responsabilitățile de extras:
array-ul fix de taskuri, inițializarea, alocarea primului slot liber, copierea
titlului, atribuirea ID-urilor, ștergerea și accesul read-only după index.
Dependențe storage: C standard, bool/size_t, limite numerice și copiere de șiruri.
Validarea JSON și a ID-ului din path, serializarea și răspunsurile HTTP rămân
în handlers. Nicio dependență nouă și nicio modificare în http_response.

Riscurile principale: ordinea sloturilor după reutilizare, monotonie fără
overflow, prioritatea FULL față de ID_EXHAUSTED când array-ul este plin,
titluri copiate în loc de pointeri la buffere temporare, stare izolată între
contexte și inițializare o singură dată pe durata serverului.

S-au implementat exact interfețele din plan:

- `void task_storage_init(struct TaskStorage *storage)`;
- `enum TaskCreateResult task_storage_create(struct TaskStorage *storage,
  const char *title, const struct Task **created)`;
- `bool task_storage_delete(struct TaskStorage *storage, int id)`;
- `const struct Task *task_storage_at(const struct TaskStorage *storage, size_t index)`.

Rezultatele create sunt OK/FULL/ID_EXHAUSTED, mapate de handler la 201/507/500.
Storage deține array-ul de 128 taskuri și next_id; `MAX_TITLE` rămâne 120 octeți.
Pointerii returnați sunt împrumutați până la următoarea mutare a contextului.
`at` întoarce NULL pentru slot inactiv sau index invalid. Titlul trebuie deja
validat și terminat NUL, de maximum 120 octeți, conform contractului din plan;
nu se introduce o politică alternativă de validare sau trunchiere în storage.

## Fișiere schimbate

| Fișier | Rol |
| --- | --- |
| `src/task_storage.h` | Modelul Task, contextul TaskStorage, constantele existente mutate, enum și contracte. |
| `src/task_storage.c` | Inițializare, create/delete/at, fără HTTP, JSON sau sockets. |
| `src/main.c` | Eliminarea globalurilor; context local inițializat explicit în main și transmis prin handle_client/route/handlers; folosirea operațiilor storage. |
| `Makefile` | Sursa/headerul nou și build/test independent pentru storage, integrat în make test. |
| `tests/test_api.py` | Test de caracterizare pentru capacitate, 507, ID-uri și ordinea sloturilor refolosite. |
| `tests/test_task_storage.c` | Șase grupuri de cazuri independente, fără framework. |
| `docs/stage-3-results.md` | Acest raport/ledger. |

`append_task` rămâne serializerul comun pentru list/create. Ștergerea păstrează
validarea ID-ului în handler, cu același 404 și același 204 fără body. Nu se
schimbă formatul JSON, MIME, headerele, limitele HTTP sau dashboardul.
Contextul este momentan deținut de main; mutarea sa în viitorul server.c aparține
etapei 6, nu acestei etape.

## Execuție incrementală și teste

1. Înaintea extragerii, testul nou de integrare a creat 128 taskuri cu ID 1..128,
   a verificat 507 și JSON exact `{"error":"task limit reached"}`, a șters ID 2,
   a creat ID 129 și a verificat poziția a doua în listă și DELETE 2 repetat ->404.
   `make test` pe codul vechi: 17/17 teste de integrare și 8 cazuri HTTP C OK.
2. Testele C storage au fost scrise înaintea implementării modulului. După
   extragerea modulului, înainte de conectarea handlerelor, build-ul independent
   GCC și `./build/test_task_storage` au trecut: 6 cazuri OK.
3. După conectarea contextului în main și înlocuirea buclelor create/delete,
   `make clean && make CC=clang && make test` a trecut: 17 teste de integrare,
   6 cazuri storage C și 8 cazuri HTTP C. Harness-urile acelei rulări au folosit
   GCC implicit, deci verificarea finală transmite CC și către make test.
4. Verificare finală explicită pentru toate binarele:
   `make clean && make CC=clang && make CC=clang test` — 17/17 integrare,
   6 cazuri storage, 8 cazuri HTTP, toate OK, fără diagnostice.
5. `make clean && make CC=gcc && make CC=gcc test` — aceleași rezultate,
   toate OK, fără diagnostice. Binarul server și ambele harness-uri rămase
   în build sunt GCC.
6. `git diff --check`: fără erori.

Testele independente verifică: două contexte independente și reinițializare,
copierea unui titlu de 120 octeți, capacitate și slot reutilizat, ID INT32_MAX-1
acceptat urmat de ID_EXHAUSTED fără slot nou, prioritatea FULL când next_id este
INT32_MAX, index 128/SIZE_MAX și ștergeri fără task corespunzător.
Sunt șase grupuri C, distincte de cele 17 metode Python.

Aceasta este o refactorizare fără comportament HTTP nou. Testele de
caracterizare au trecut înainte și după extragere; nu s-a fabricat un eșec RED.
Nu a fost necesară corectarea unui bug sau aplicarea unui ciclu TDD pentru
comportament nou. Nu s-au observat eșecuri în această etapă.

Comparația automatizată cu HEAD confirmă că 15 funcții fără legătură cu storage
(parser HTTP, helpers JSON/Unicode, serializerul de task, pagina și validarea
portului) sunt identice textual. http_response.c/h, test_http_response.c și
web/index.html sunt identice octet cu octet cu versiunea comisă.

Task 3: complete (base f1a1449, changes uncommitted; Clang/GCC: integration
17/17, storage C 6 cases, HTTP C 8 cases; diff check clean).

## Review, probleme și limitări

Review independent read-only prin `requesting-code-review`: fără constatări
critice sau importante. Reviewerul a compilat și rulat independent harness-ul
storage cu GCC și flagurile stricte: 6 cazuri OK. Nu a rulat independent testele
socket și nu a revizuit acest raport, redactat după verdict.

- Final: minor (deferred): testele nu verifică explicit păstrarea valorii
  pointerului `created` la fiecare eșec, deși contractul o promite și implementarea
  o respectă. Se poate întări această aserțiune într-o schimbare ulterioară de teste.
- Ruling: se păstrează precondiția titlu validat și context/output non-NULL din
  interfața stabilită. Nu se adaugă rezultate enum sau politici de validare.
  Cost: un viitor apelant C trebuie să respecte explicit contractul; handlerul
  actual validează toate datele externe înainte de create.
- Ruling: parserul, politica Unicode și mutarea storage înainte de serializare
  rămân neschimbate și în afara extragerii. Cost: riscurile existente nu sunt
  remediate aici; orice schimbare ulterioară necesită teste și decizie separată.
- Ruling: se lucrează în checkoutul existent, pe ramura existentă, cu acest raport
  ca ledger persistent; fără worktree nou, cleanup prin ștergere sau commit al
  etapei 3. Cost: modificările rămân vizibile și necomise în checkout.
- Review-ul ramurilor este manual; nu s-a rulat un instrument de complexitate
  ciclomatică și nu se raportează scoruri măsurate. Funcțiile modulului sunt
  scurte și focalizate; testul de integrare păstrează secvența completă de
  capacitate/reutilizare fără fragmentare artificială.
- Integrarea folosește socketuri localhost cu rulare autorizată în afara
  sandboxului. Testele C independente nu necesită socketuri sau dependențe noi.
- ID_EXHAUSTED se testează direct în C prin fixture; nu se emit miliarde de
  cereri HTTP pentru a atinge natural această stare.

### Observații separate, în afara etapei 3

Nu s-a identificat un defect nou de securitate în această extragere. Rămân
observațiile existente: parserul tokenizează CR/LF individual, octeții UTF-8
direcți nu sunt validați ca secvențe Unicode, iar uploadul supradimensionat
poate produce reset de conexiune. Acestea nu sunt remediate sau prezentate
drept buguri noi reproduse în această sesiune. Erorile de transport HTTP
rămân fără propagare, conform contractului existent.

## Recomandare pentru etapa 4

Urmează extragerea HTTP parser conform planului: teste raw pentru Content-Length
lipsă/duplicat/invalid/overflow, Transfer-Encoding, limite de header/body,
recepție fragmentată, body NUL și EOF prematur înaintea mutării codului.
Helperul raw existent poate fi reutilizat. Nu schimba simultan politica de
acceptare HTTP; diferențele observate se investighează separat.
Etapa 4 nu a fost începută.

## Git status final așteptat după acest raport

```text
 M Makefile
 M src/main.c
 M tests/test_api.py
?? backups/
?? docs/stage-3-results.md
?? src/task_storage.c
?? src/task_storage.h
?? tests/test_task_storage.c
```

Indexul este gol. Backupul existent rămâne necomis. Niciun commit al etapei 3,
nicio instalare și niciun push.

## Verificare pentru finalizarea etapei 3

Observația despre `created` a fost examinată: nu este un defect de implementare.
Cele două ieșiri de eșec (FULL și ID_EXHAUSTED) nu scriu în output; atribuirea
apare exclusiv pe succes. Testele au fost întărite înainte de commit cu un
pointer sentinel, verificând explicit păstrarea lui în toate cazurile de eșec
deja exercitate, inclusiv FULL combinat cu epuizarea ID-urilor. Nu s-a schimbat
codul de producție. Secțiunile anterioare reprezintă raportul inițial.
Reverificare înainte de commit: build și `make CC=clang test`, respectiv
`make CC=gcc test`, după clean: 17/17 teste Python, 6 cazuri storage C și
8 cazuri HTTP C pentru fiecare compilator; fără diagnostice. Diff check curat.
