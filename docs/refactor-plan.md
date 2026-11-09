# CServe Pi — plan de refactorizare

> Pentru executare ulterioară: folosește `superpowers:executing-plans`, pas cu
> pas. Acest document este o propunere; etapa actuală se oprește la documente.

**Scop:** separarea responsabilităților serverului fără schimbarea API-ului.
**Arhitectură:** extrageri succesive din `src/main.c` în server, parser HTTP,
router și task storage, cu interfețe explicite și stare deținută clar.
**Tehnologii:** C17, Linux ARM64, POSIX sockets, Python standard library.
**Specificație:** `docs/design.md`, `README.md`, `AGENTS.md` și cerințele acestei etape.

## Limite și alternative

Nu se schimbă acum `src/`, `tests/`, `Makefile` sau comportamentul. Nu se
instalează dependențe, nu se șterg fișiere și nu se execută commit/push.
Comanda `make clean` cerută elimină doar artefactele generate din `build/`;
acestea sunt regenerate prin compilare. Pașii de implementare de mai jos nu
sunt executați în această etapă.

Alternative analizate: păstrarea monolitului cu helpers reduce diff-ul, dar nu
separă transportul de domeniu; rescrierea tuturor modulelor simultan crește
riscul regresiilor. Recomandarea autorului planului este extragerea incrementală
în cele cinci componente cerute, cu suită verde între extrageri. Nu presupune
aprobarea implementării.

Păstrăm serverul secvențial IPv4, o cerere per conexiune, închidere explicită,
stocare exclusiv în memorie, default `127.0.0.1:8080`, opțiunea LAN explicită,
128 taskuri și titluri de maximum 120 octeți. Păstrăm limitele pentru header
8192, body 4096, JSON 65536 și pagină 32768 octeți, inclusiv semantica actuală
la limită. Fără autentificare, TLS, keep-alive, chunked, DB sau threading nou.
Păstrăm toate flagurile stricte din Makefile și nu introducem dependențe.

## Stare inițială verificată — 2026-11-09

Au fost citite integral `AGENTS.md`, `README.md`, `docs/design.md`, `Makefile`,
`src/main.c` și `tests/test_api.py`.

Comandă: `make clean && make CC=clang && make test`.
Compilarea Clang a reușit fără diagnostice; 9 teste au trecut, `OK`, exit 0.
Prima rulare în sandbox a compilat, dar toate testele au fost blocate în
`socket.socket()` cu `PermissionError: Operation not permitted`; repetarea
autorizată în afara sandboxului a trecut. Nu a fost necesară schimbarea codului.
`make test` a folosit binarul Clang deja construit; nu reprezintă un build GCC.
`uname -sm`: `Linux aarch64`. Nu s-a verificat modelul hardware, deci rezultatul
nu este prezentat drept test efectuat pe o placă Raspberry Pi identificată.
Directorul nu este recunoscut drept repository Git; nu există diff Git disponibil.

## Analiză manuală a codului actual

Referințele de linii sunt pentru fișierul inițial, de 459 linii. Intervalele
includ semnătura și acoladele; lungimea nu este complexitate ciclomatică.
Nu s-a rulat un instrument de complexitate și nu se atribuie scoruri numerice.

| Funcții / linii | Observație și acțiune propusă |
| --- | --- |
| `main` 416–459 (44 linii), `valid_port` 407–414 | CLI, validare host, semnale, listener, accept și cleanup în aceeași funcție. Separă configurarea de server. Conversia portului și a ID-ului seamănă, dar politicile lor diferă; nu le unifica automat. |
| `read_request` 132–168 (37), `parse_headers` 107–130 (24) | Primele ținte pentru analiza ramurilor: recv/EINTR/EOF, framing, limite, parsing și copiere. Separă parsarea request-line/headerelor de colectarea octeților, păstrând ordinea verificărilor. |
| `parse_content_length` 93–104, `find_header_end` 89–91 | Helpers deja focalizați; păstrează protecția overflow și delimitatorul comun, fără noi abstracții inutile. |
| `route` 379–395 | Scurt, dar cu decizii multiple. Repetă `/api/tasks`; prefixul DELETE este repetat în `delete_task`, cu lungime hardcoded 11. Constante comune și handlers clare; păstrează diferența 404/405. |
| `create_task` 327–343, `delete_task` 345–366 (22), `list_tasks` 312–325 | Amestecă validare, căutare/mutare în array, serializare și HTTP. Buclele au selecții diferite (slot liber, ID, activ); extrage operațiile storage, nu un iterator generic. Globalurile `tasks` și `next_id` le cuplează. |
| `get_title` 259–276, `read_json_string` 237–257, `decode_escape` 218–235 | Parser JSON îngust, cu validare de domeniu și multe ieșiri timpurii. Izolează ca helpers private ale routerului inițial; testează surrogate pairs și limite în octeți înainte de simplificare. |
| `put_utf8` 193–216 (24), `hex4` 176–191, `skip_space` 170–173 | Cod de encoding cu măști și limite Unicode literale. Denumește semantic constantele; nu fragmenta artificial ramurile UTF-8 pentru ținta de 20 linii. |
| `append` 278–285, `json_append_title` 287–301, `append_task` 303–310 | Reutilizare deja existentă pentru JSON; nu duplica encoderul pentru list/create. `append` presupune `pos < cap`; documentează invariantul și testează separat orice întărire a contractului. |
| `send_all` 48–56, `respond` 58–72, `json_reply` 74–76, `error_reply` 78–87 | Helpers HTTP deja utili. Statusuri/reason/MIME repetate; centralizează metadatele păstrând exact textele. `send_all` ascunde eșecul; schimbarea propagării erorilor ar fi o schimbare separată, testată. |
| `serve_page` 368–377 | Citire fișier și răspuns, responsabilitate compactă; păstrează calea fixă și comportamentul la limită. Nu adăuga rezolvare arbitrară de paths. |
| `handle_client` 397–405 | Configurare timeout și orchestrare; timeout literal 5 duplicat conceptual între recv/send. Helper local de configurare dacă aduce claritate. |

Exemple suplimentare de constante: capacitățile method/path/version 16/160/16
și lățimile `sscanf` 15/159/15 trebuie să rămână sincronizate; header reply 512,
prefix JSON 64, overhead creare 128, backlog 16, port maxim 65535,
`HTTP/1.1`, nume de headere, MIME, `title`, calea paginii și mesajele CLI.
Există deja constante corecte pentru limitele mari și helpers reutilizați:
problema nu este absența totală a structurii. Cifrele de sintaxă, terminatorul
NUL și operațiile evidente 0/1 nu trebuie ascunse prin constante fără sens.

Zone de atenție, fără a declara buguri reproduse: tokenizerul headerelor
tolerează delimitatori individuali CR/LF; octeții UTF-8 direcți nu sunt validați
ca secvențe Unicode; `strtol` pentru port are altă politică lexicală decât
parserul Content-Length; rezultatele `setsockopt` sunt ignorate; stocarea este
mutată înainte de serializarea răspunsului de creare. Orice întărire de validare
sau schimbare de erori trebuie separată de extrageri și demonstrată prin teste.

## Arhitectură propusă și interfețe

```text
main -> server -> HTTP parser
              -> router -> task storage
                        -> HTTP response helpers
```

| Fișiere viitoare | Responsabilitate și interfață propusă |
| --- | --- |
| `src/main.c` | `int main(int argc, char **argv)`; helpers private CLI, construirea configurației și apel `server_run`. |
| `src/server.h`, `src/server.c` | `struct ServerConfig { const char *host; uint16_t port; }; int server_run(const struct ServerConfig *config);` Deține listenerul, clienții, semnalele, timeouturile și o instanță storage inițializată la pornire; închide fd-urile exact o dată. |
| `src/http_parser.h`, `src/http_parser.c` | `struct Request` cu limitele actuale; `int http_read_request(int fd, struct Request *req);` întoarce 0 sau statusul HTTP actual. Ieșirea este inițializată de apelant, corpul este copiat și terminat NUL; helpers de parsare private. |
| `src/http_response.h`, `src/http_response.c` | Componentă suport HTTP: `void http_respond(int fd, int status, const char *reason, const char *mime, const char *body, size_t length); void http_json_reply(int fd, int status, const char *reason, const char *json); void http_error_reply(int fd, int status);` Extrage fidel funcțiile existente; send rămâne privat. |
| `src/router.h`, `src/router.c` | `void router_handle(int fd, const struct Request *req, struct TaskStorage *storage);` Selectează rutele, validează titlul/ID-ul, serializează și mapează rezultatele storage la statusuri. Helpers JSON și pagina rămân private inițial. |
| `src/task_storage.h`, `src/task_storage.c` | `struct Task`, `struct TaskStorage` (array fix și next_id); `void task_storage_init(struct TaskStorage *storage); enum TaskCreateResult task_storage_create(struct TaskStorage *storage, const char *title, const struct Task **created); bool task_storage_delete(struct TaskStorage *storage, int id); const struct Task *task_storage_at(const struct TaskStorage *storage, size_t index);` |

`TaskCreateResult` are `TASK_CREATE_OK`, `TASK_CREATE_FULL`,
`TASK_CREATE_ID_EXHAUSTED`; routerul le mapează la 201/507/500. Storage acceptă
doar titlu validat, NUL-terminated de maximum 120 octeți; copiază titlul.
`task_storage_at` întoarce NULL pentru slot inactiv sau index >=128.
Pointerii returnați sunt împrumutați până la următoarea mutare; nu se eliberează.
Iterarea păstrează ordinea sloturilor, inclusiv reutilizarea după ștergere,
nu sortează după ID. `next_id == INT32_MAX` păstrează refuzul actual de creare.
Storage nu cunoaște socketuri, HTTP sau JSON. Routerul nu deține fd-ul.

Constantele aparțin modulelor: limite HTTP în headerul parserului, task/title
în storage, rute și JSON keys în router, MIME/statusuri în suportul HTTP,
timeout/backlog în server. Nu introduce un header global cu toate constantele.
Nu schimba layout-uri prin packing, nu presupune că `long` are o lățime fixă,
păstrează `size_t`, verificările de overflow și conversia `htons`.

## Teste și review: riscuri prioritare

Suita actuală are 9 metode: health, dashboard, listă goală, create/read/delete,
escaping, Unicode escape, taskuri invalide, 404/405 și body prea mare. Nu dovedește
toate limitele sau ramurile de eroare. Prioritățile sunt:

1. Framing HTTP: Content-Length lipsă/duplicat/invalid și Transfer-Encoding — etapa 4.
2. Recepție fragmentată, EOF și NUL în body — etapa 4.
3. Capacitate storage, slot refolosit și ID monoton — etapa 3.
4. Limite în octeți și surrogate pairs invalide — etapa 5.
5. CLI invalid, socket ocupat și fd-uri închise corect — etapa 6.

## Pași de implementare ulterioară

Pentru fiecare etapă: teste de caracterizare pe implementarea veche, extragere
minimă, toate testele din nou, apoi review al diff-ului. Testele de caracterizare
pot trece imediat: refactorizarea pur structurală nu cere o regresie artificială.
Dacă un test exprimă comportament nou, separă-l: RED observat cu `make test`,
implementare minimă GREEN, apoi REFACTOR. Nu continua peste un eșec neexplicat.
Niciun commit/push până la o autorizare ulterioară.

### Etapa 1 — fixarea contractului observabil

Fișier viitor modificat: `tests/test_api.py`; fără schimbare C.

- [ ] Adaugă teste pentru headerele `Connection: close`, `Cache-Control: no-store`,
  `X-Content-Type-Options: nosniff` și Content-Length egal cu numărul de octeți.
- [ ] Verifică MIME exact: JSON normal `application/json; charset=utf-8`, DELETE
  204 `application/json`, body gol; health și mesajele de eroare rămân identice.
- [ ] Caracterizează DELETE pe ID gol, 0, negativ, alfanumeric și >INT32_MAX:
  404; POST `/health` și GET `/api/tasks/1`: 404; PUT `/api/tasks`: 405.
- [ ] Rulează `make clean && make CC=clang && make test`: toate testele trec.
  Dacă observațiile diferă, investighează înainte de a fixa contractul în teste.

### Etapa 2 — suportul pentru răspunsuri HTTP

Creează `src/http_response.{h,c}`; modifică `src/main.c`, `Makefile`.
Interfețe produse: cele trei funcții `http_*reply/respond` definite mai sus.

- [ ] Extrage `send_all`, `respond`, `json_reply`, `error_reply` fără schimbare
  de erori, reason phrases sau tipuri MIME; redenumește apelurile mecanic.
- [ ] Denumește statusurile, MIME și capacitatea headerului în modulul HTTP;
  păstrează parametrul MIME pentru diferența răspunsului 204.
- [ ] Actualizează Makefile cu lista explicită de surse și headere ca dependențe;
  regula actuală `$<` compilează doar primul fișier, deci trebuie înlocuită cu
  lista surselor, fără a trimite headerele la linker.
- [ ] Rulează build-ul Clang curat și `make test`; compară headerele din etapa 1.

### Etapa 3 — task storage independent

Creează `src/task_storage.{h,c}`; modifică `src/main.c`, `Makefile`,
`tests/test_api.py`. Consumă/producere: interfețele storage din tabel.

- [ ] Adaugă test care creează 128 taskuri (201, ID 1..128), apoi primește 507
  cu `{"error":"task limit reached"}`; șterge ID 2, creează ID 129 și verifică
  plasarea în al doilea slot al listei. Repetarea DELETE pe ID 2 produce 404.
- [ ] Rulează `make test` înainte de extragere și păstrează rezultatul.
- [ ] Mută array-ul și next_id într-un context inițializat explicit. Înlocuiește
  buclele din handlers prin create/delete/at; păstrează serializarea comună.
- [ ] Verifică separat limita next_id printr-un test C mic
  `tests/test_task_storage.c`, fără framework: setează next_id=INT32_MAX,
  cere create, așteaptă ID_EXHAUSTED și niciun slot activ nou. Integrează-l în
  `make test`; verifică și index 128 -> NULL și două contexte independente.
- [ ] Build curat Clang, toate testele, review al ordinii sloturilor și al copiilor.

### Etapa 4 — HTTP parser

Creează `src/http_parser.{h,c}`; modifică `src/main.c`, `Makefile`,
`tests/test_api.py`. Produce `Request` și `http_read_request` din tabel.

- [ ] Adaugă helper Python pentru cereri raw cu socket local și timeout finit.
  Testează POST fără Content-Length ->411; duplicate, negativ, text și overflow
  ->400; Transfer-Encoding ->400; Content-Length 4097 ->413.
- [ ] Testează header fără terminator care ajunge la 8192 octeți ->413;
  request valid împărțit în mai multe trimiteri ->201 (nu presupune limite de
  pachete TCP); body cu NUL ->400; body incomplet și shutdown(SHUT_WR) ->400.
- [ ] Rulează `make test` pe codul vechi, apoi extrage funcțiile parserului fidel.
- [ ] Separă request-line, un header și copierea body doar unde scade încărcarea
  logică; menține ordinea erorilor și sincronizarea capacităților cu `sscanf`.
- [ ] Build curat Clang și toate testele; review manual al overflow-ului și al
  ramurilor, fără a atribui un scor de complexitate nemăsurat.

### Etapa 5 — router și helpers JSON

Creează `src/router.{h,c}`; modifică `src/main.c`, `Makefile`,
`tests/test_api.py`. Consumă parser/storage/response; produce `router_handle`.

- [ ] Caracterizează titlu de 120 octeți ->201, 121 ->400; titlu multioctet de
  120/122 octeți ->201/400; surrogate high/low izolat ->400; pereche validă ->201;
  control escaped ->400, câmp suplimentar sau JSON trailing ->400.
- [ ] Rulează `make test`, apoi mută route/handlers/pagina și helpers JSON.
- [ ] Centralizează paths și elimină lungimea 11 folosind lungimea prefixului;
  păstrează statusurile 404/405 și calea fixă `web/index.html`.
- [ ] Păstrează `append_task` comun pentru list/create, extrage serializarea
  listei din trimiterea HTTP dacă clarifică responsabilitatea. Nu adăuga un
  framework de rutare sau parser JSON general.
- [ ] Build curat Clang și toate testele; review al encodingului și capacităților.

### Etapa 6 — server și main minimal

Creează `src/server.{h,c}`; modifică `src/main.c`, `Makefile`,
`tests/test_api.py`. Consumă `http_read_request`/`router_handle`; produce
`ServerConfig`/`server_run` din tabel.

- [ ] Caracterizează CLI în subprocess: opțiune necunoscută, argument lipsă,
  port 0/65536 și host nepermis ->exit 2. Testează port ocupat ->exit 1;
  verifică health după închiderea prematură a unui client.
- [ ] Rulează `make test`; extrage listenerul, accept-loop, timeouturile și
  handle_client. Serverul deține fd-urile și contextul storage; main parsează
  argumentele și returnează rezultatul serverului.
- [ ] Denumește backlog 16, timeout 5 secunde, limitele portului și exit codes.
  Nu schimba politica actuală pentru erorile setsockopt/send în această mutare.
- [ ] Build curat Clang și toate testele; review al cleanup-ului pe fiecare
  ieșire, al EINTR, SIGPIPE și al defaultului loopback.

### Etapa 7 — verificare finală și documentare

Modifică ulterior `README.md`, `docs/design.md` pentru structura efectivă.

- [ ] Review pe toate modulele: fără duplicări noi, constante cu sens, funcții
  cu o responsabilitate, aproximativ 20 linii fără fragmentare artificială.
- [ ] Verifică manual ramurile fiecărei funcții; obiectiv sub 10, maxim 20.
  Dacă există deja un instrument de complexitate disponibil, consemnează
  comanda, versiunea și convenția lui. Fără instrument, declară explicit că
  pragul numeric nu a fost verificat automat; nu inventa scoruri.
- [ ] Rulează `make clean && make CC=clang && make test`, apoi
  `make clean && make CC=gcc && make test`, dacă ambele compilatoare există.
- [ ] Rulează `make debug && make test` dacă runtime-ul sanitizer este disponibil;
  consemnează orice limitare, fără instalare automată.
- [ ] Repetă build/test nativ pe Raspberry Pi OS ARM64 identificat; raportează
  exact mediul și rezultatele, fără a extrapola rezultatele locale.
- [ ] Solicită review prin `requesting-code-review` înainte de integrarea
  implementării. Oprește-te dacă API-ul diferă sau apar erori neexplicate.

## Criteriu de încheiere pentru etapa actuală

Există regulile Cursor, regulile explicite Codex în AGENTS și acest plan.
Implementarea, testele și Makefile rămân identice; build/test de bază este
raportat cu limitările mediului. Implementarea planului necesită o etapă nouă.
