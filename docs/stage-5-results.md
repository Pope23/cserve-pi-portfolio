# SDD ledger — plan: docs/refactor-plan.md

## Mediu și finalizarea etapei 4 — 2026-11-09

Mediu confirmat: `Raspberry Pi test host`, `aarch64`, Raspberry Pi 5 Model B Rev 1.1,
`<project-root>`.

Au fost examinate diff-ul complet și fișierele noi ale etapei 4. Verificarea
suplimentară a `docs/stage-3-results.md` a arătat **niciun diff necomis**:
corecția despre `created` și verificările aferente erau deja incluse legitim
în `c16ee14`. Nu exista o modificare neașteptată și nu s-a comis din nou acel
fișier. Concluzia a fost documentată în raportul etapei 4.

S-a adăugat `/backups/` în .gitignore, conform cererii. `git check-ignore` confirmă
excluderea arhivei existente. Backupurile nu au fost șterse sau adăugate în Git.

Înainte de commit, build-urile curate și suita completă, cu CC explicit atât
la build cât și la test, au trecut cu Clang și GCC: **25 metode Python și 20
grupuri C** fiecare (8 HTTP response, 6 storage, 6 parser), fără diagnostice.
Diff check și verificarea whitespace a fișierelor noi au trecut.

Commit local: `9d7df92`, `refactor: extract HTTP parser module`.
Conține explicit .gitignore, Makefile, main.c, http_parser.c/h, test_api.py,
test_http_parser.c și stage-4-results.md. Nu s-a folosit git add -A și nu s-a
executat push. Etapa 5 începe de la acest commit.

## Arhitectura implementată în etapa 5

Interfața publică este exact cea din plan:
`void router_handle(int fd, const struct Request *req, struct TaskStorage *storage)`.

```text
main: CLI, listener, accept, timeouturi, citire Request
  -> http_read_request
  -> router_handle
       -> validare JSON/ID și handlers private
       -> task_storage_create/delete/at
       -> helpers JSON comuni pentru create/list
       -> http_respond/json_reply/error_reply
       -> pagina fixă web/index.html
```

Routerul împrumută fd-ul și Request-ul parsate. Contextul storage este inițializat
și deținut în continuare de main. Routerul nu închide socketul și nu schimbă
timeouturi sau politica SIGPIPE. Citirea paginii închide fișierul inclusiv înainte
de tratarea erorilor de citire; calea rămâne fixă, fără input de la utilizator.

Au fost mutate handlers, ruta health, pagina și helpers JSON/Unicode din main.c
în router.c. Paths, metodele, cheia title, textele JSON, dimensiunile bufferelor
și parametrii encodingului au constante locale denumite. Prefixul DELETE are
lungimea derivată prin sizeof, eliminând valoarea duplicată 11.
Serializarea listei este separată de trimiterea HTTP. `append_task` rămâne comun
pentru listă și creare, fără al doilea encoder JSON sau framework nou.

Capacitățile existente sunt păstrate: titlu 120 octeți, listă JSON 65536, pagină
32768 și răspuns creare 368 octeți. Limitele de header/body și structurile storage
nu sunt schimbate. Precedența rutelor, 404/405, 204 fără body, JSON, MIME și
headerele HTTP rămân aceleași. Main păstrează responsabilitățile de server pentru
etapa 6; nu s-a început extragerea server.c.

## Fișierele etapei 5

| Fișier | Modificare |
| --- | --- |
| `src/router.h` | Contractul router_handle și declarații forward pentru tipurile împrumutate. |
| `src/router.c` | Rutare, handlers taskuri, pagina, validare JSON/ID și serializare private. |
| `src/main.c` | Eliminarea blocului extras și apelul router_handle; CLI/server rămân. |
| `Makefile` | Adăugarea sursei și headerului routerului la dependențele build-ului. |
| `tests/test_api.py` | Șapte teste noi de caracterizare și un helper pentru body JSON raw. |
| `docs/stage-5-results.md` | Acest raport/ledger. |

Comparația automatizată cu HEAD confirmă că main.c diferă numai prin extragerea
routerului/helpers, includerea headerului, eliminarea constantelor/include-ului
mutate și redenumirea apelului. http_parser.c/h, task_storage.c/h, http_response.c/h
și dashboardul sunt identice octet cu octet cu versiunea comisă. Interfețele
modulelor finalizate nu au fost modificate.

## Teste înainte și după refactorizare

Înaintea implementării, `make test` a trecut pe codul vechi: **32/32 metode
Python + 20 grupuri C**. Cele șapte metode noi fixează:

1. Titlu ASCII de 120 octeți acceptat, 121 respins.
2. Titlu UTF-8 de 120 octeți acceptat, 122 respins.
3. Surrogate high/low izolat și perechi invalide respinse; pereche validă acceptată.
4. Control escaped, inclusiv NUL, respins.
5. Câmp suplimentar, cheie duplicată, JSON trailing și șiruri incomplete respinse.
6. Titlu maxim compus din ghilimele/backslash, round-trip create/list și lungime HTTP.
7. Paths asemănătoare și sufixe DELETE invalide păstrează 404, fără extinderea rutelor.

Pași incrementali: teste de caracterizare pe implementarea veche; mutare mecanică
în modul și suită verde; centralizarea constantelor și separarea serializării;
verificare de memorie și build-uri finale. Nu s-a introdus comportament nou și
nu s-a fabricat un RED pentru extragerea structurală. Niciun bug nu a necesitat
un ciclu RED/GREEN în această etapă.

### Rezultate finale reale

| Verificare | Rezultat |
| --- | --- |
| `make clean && make CC=clang && make test` | 32/32 Python, HTTP C 8, storage C 6, parser C 6; toate OK. |
| Harness-uri reconstruite explicit cu `make CC=clang -B build/test_http_response build/test_task_storage build/test_http_parser`, apoi executate separat | 20 grupuri C OK cu Clang. |
| `make clean && make CC=gcc && make test` | 32/32 Python și aceleași 20 grupuri C, toate OK. |
| Clang ASan+UBSan, toate binarele instrumentate, `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer` și toate warnings stricte | 32/32 Python + 20 grupuri C OK. |
| Reexecutarea suitei instrumentate cu UBSAN_OPTIONS=halt_on_error=1 și ASAN_OPTIONS=halt_on_error=1 | 32/32 Python + 20 grupuri C OK. |
| `git diff --check` și whitespace în fișierele noi | Fără erori. |

În comanda cu `make test` fără CC, harness-urile C sunt construite implicit cu
GCC; de aceea verificarea lor Clang a fost explicită. Cele 32 sunt metode
unittest; subtestele și aserțiunile nu sunt numărate separat. Cele 20 sunt grupuri
în trei programe C. Nu s-a adăugat un harness C nou pentru router; integrarea
reală îi verifică efectele prin endpointuri.

Nu au apărut warnings sau eșecuri de test în build-urile finale. Nu s-a observat
o eroare de memorie/UB pe cazurile instrumentate. Integrarea oprește serverul prin
SIGTERM; nu se revendică o verificare completă de leak-uri la shutdown normal.
Testele localhost au rulat autorizat în afara sandboxului. Nu s-au instalat
dependențe. La final toate artefactele din build sunt GCC fără sanitizers.

Task 5: complete (base 9d7df92, changes uncommitted; Clang/GCC Python 32/32,
C groups 20/20; ASan/UBSan suite OK; diff check clean).

## Review, probleme și limitări

Review independent read-only prin requesting-code-review: fără probleme critice
sau importante. Reviewerul a comparat implementarea cu main.c din HEAD și a
confirmat păstrarea comportamentului și interfeței. Nu a rulat independent
testele și nu a revizuit acest raport, redactat după verdict.

- Final: minor (deferred): se poate documenta explicit precondiția `*pos < cap`
  în helperul privat append. Toate apelurile actuale o păstrează; nu este un
  defect de bounds demonstrat. Bufferul are capacitate fixă nenulă, poziția
  începe de la zero și crește numai după verificarea spațiului disponibil.
- Ruling: nu se întărește arbitrar validarea argumentelor helperului privat sau
  validarea UTF-8 directă. Cost: contractele interne și politica de acceptare
  existente rămân; o schimbare viitoare necesită teste și decizie separată.
- Ruling: CLI, socket options și cleanup-ul serverului rămân în etapa 6.
  Cost: structura și limitările actuale ale ciclului de viață sunt păstrate.
- Ruling: se lucrează în checkoutul existent cu raportul ca ledger persistent,
  fără worktree nou sau commit al etapei 5. Cost: modificările rămân vizibile și
  necomise în checkoutul curent.
- Ramurile au fost examinate manual. Nu s-a rulat o unealtă de complexitate
  ciclomatică și nu se prezintă scoruri măsurate. Helpers Unicode păstrează
  secvențele coerente de encoding, fără fragmentare artificială pentru 20 linii.
- Limitări existente păstrate: taskurile sunt mutate înaintea serializării
  răspunsului de creare; erorile send nu sunt propagate; validarea UTF-8 directă
  este incompletă. Nu au fost descoperite defecte noi în această etapă.

## Recomandări pentru etapa 6

Urmează server.c/h și main minimal, exact conform planului: teste pentru CLI
invalid, port ocupat și client închis prematur; apoi extragerea listenerului,
accept-loop, timeouturilor și contextului storage. Păstrează defaultul loopback,
EINTR, SIGPIPE și închiderea fiecărui fd exact o dată. Etapa 6 nu a fost începută.

## Git status final

```text
 M Makefile
 M src/main.c
 M tests/test_api.py
?? docs/stage-5-results.md
?? src/router.c
?? src/router.h
```

Index gol. Backupurile existente sunt ignorate prin /backups/ și păstrate local.
Niciun commit al etapei 5 și niciun push.

## Pregătirea finalizării etapei 5

Observația minoră a fost acceptată: precondiția `*pos < cap` este documentată
explicit lângă append. Apelanții rezervați helperului păstrează invariantul;
schimbarea este exclusiv de comentariu, fără modificarea comportamentului.
Verificarea diff-ului după adăugarea explicită în index a detectat și o linie
goală suplimentară la finalul noului router.c. A fost eliminată înainte de
commit; verificarea anterioară a fișierului necomis nu detectase acest caz EOF.
