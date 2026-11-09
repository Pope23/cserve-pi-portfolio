# CServe Pi — arhitectura modulară

Server educațional C17 pe Linux/POSIX, secvențial IPv4, cu API de taskuri și
un dashboard static. Extragerile păstrează contractul inițial, fără framework,
DB, threading, TLS sau autentificare.

## Module și direcția dependențelor

```text
main -> server -> http_parser
               -> router -> task_storage
                         -> http_response
               -> task_storage (inițializare și ownership)
               -> http_response (erori de citire)
```

- `main.c`: opțiuni --host/--port, validare și apel server_run.
- `server.c/h`: ServerConfig, socket/bind/listen/accept, timeouturi și cleanup.
- `http_parser.c/h`: Request, http_read_request și helpers private de framing.
- `router.c/h`: router_handle, validarea JSON/ID, handlers și pagina fixă.
- `http_response.c/h`: http_respond, http_json_reply și http_error_reply.
- `task_storage.c/h`: Task/TaskStorage, init/create/delete/at și ID-uri monotone.

Parserul folosește enumul de status HTTP din headerul response, dar nu trimite
răspunsuri. Storage nu depinde de HTTP, JSON sau socketuri. Routerul nu gestionează
listenerul. Toate modulele publice au header propriu; helpers sunt static.

## Contracte și ownership

`int server_run(const struct ServerConfig *config)` împrumută configurația validată,
deține listenerul, clienții și un singur context storage. Un client este închis
exact o dată după tratare; listenerul se închide la eșec bind/listen sau după o
eroare accept nerecuperabilă. Un eșec socket nu produce close pe un fd invalid.
Accept reia EINTR. SIGPIPE este ignorat. Rezultatele setsockopt/close și erorile
send nu sunt propagate, conform comportamentului păstrat.

`int http_read_request(int fd, struct Request *req)` împrumută fd-ul, cere Request
inițializat la zero și întoarce 0 sau un status HTTP. Copiază body-ul și termină
NUL; nu întoarce pointeri în bufferul temporar. Outputul parțial nu se folosește
la eroare. Excesul de octeți după prima cerere este ignorat, fără keep-alive.

`void router_handle(int fd, const struct Request *req, struct TaskStorage *storage)`
împrumută toate argumentele și emite un răspuns. Helpers JSON rămân private,
cu append_task comun pentru creare și listare. Pagina are calea fixă
web/index.html, fără rezolvare de paths arbitrare.

Storage copiază titlul deja validat, de maximum 120 octeți și terminat NUL.
Pointerii returnați sunt împrumutați până la următoarea mutare. at întoarce NULL
pentru slot inactiv sau index invalid. Create produce OK, FULL sau ID_EXHAUSTED;
pe eșec nu modifică storage sau pointerul output. Listarea păstrează ordinea
sloturilor, inclusiv după reutilizare, fără sortare după ID.

## Contract HTTP și limite

| Cerere | Rezultat normal |
| --- | --- |
| GET / | 200, dashboard |
| GET /health | 200, JSON status ok |
| GET /api/tasks | 200, obiect cu array tasks |
| POST /api/tasks | 201, obiect id/title |
| DELETE /api/tasks/:id | 204, Content-Length 0 și niciun body |

128 taskuri în memorie, pierdute la restart; titlu maximum 120 octeți, nu caractere.
Header maximum 8192, body maximum 4096, răspuns listă 65536 și pagină 32768 octeți.
Request method/path au 15/159 octeți utili. Bufferul de creare are 368 octeți.
ID-ul INT32_MAX nu se alocă; un array plin produce FULL înaintea verificării ID.

Content-Length este obligatoriu pentru POST. Duplicatele, overflow-ul, valori
nevalide, Transfer-Encoding și NUL în body sunt respinse. Limitele și ordinea
erorilor sunt testate. API-ul păstrează 400/404/405/411/413/500/507 existente.
Răspunsurile au Content-Length explicit, Connection: close, Cache-Control:
no-store și X-Content-Type-Options: nosniff. MIME pentru DELETE 204 rămâne
application/json; celelalte răspunsuri JSON au charset=utf-8.

## Semnale, timeouturi și securitate

Default 127.0.0.1:8080; 0.0.0.0 este explicit pentru demonstrații LAN.
Nu expune API-ul neautentificat direct pe Internet. Timeouturile recv/send de
5 secunde sunt pentru apeluri, nu un deadline total: progresul lent poate ocupa
serverul secvențial. Eșecul setsockopt este ignorat, deci timeoutul nu este o
garanție dacă sistemul refuză opțiunea.

SIGTERM/SIGINT nu au handler propriu: termină procesul imediat, kernelul închide
fd-urile, fără drenarea clientului sau cleanup-ul normal al aplicației. LSan nu
face verificarea normală la ieșirea prin aceste semnale. Oprirea grațioasă și un
deadline total ar schimba comportamentul și necesită un scop separat.

Parserul păstrează tokenizerul tolerant CR/LF și subsetul HTTP educațional.
UTF-8 direct nu este validat complet; surrogate escapes și limitele în octeți
sunt validate. Dashboardul folosește textContent, nu innerHTML pentru taskuri.
Nu s-a modificat comportamentul frontendului; maxlength HTML nu exprimă limita
UTF-8 în octeți, care este impusă de server.

## Verificare și limitele dovezilor

make test rulează integrarea Python și patru harness-uri C. Integrarea folosește
serverul real și socketuri localhost, verificând CRUD, rute, headere, framing,
Unicode, limite, timeout, CLI, descriptori și semnale. Harness-urile folosesc
interceptări GNU la link pentru erori greu de forțat reproductibil, fără hooks
în producție. Acestea nu sunt o probă pentru toate condițiile din kernel.

Testat nativ pe Pi 5 ARM64, Clang/GCC și ASan/UBSan; rezultatele exacte și auditul
sunt în docs/stage-7-results.md. Nu se revendică certificare de securitate,
compatibilitate HTTP completă, absența tuturor leak-urilor sau validare vizuală
în browser pentru dashboard. Refactorizarea nu introduce dependențe externe.
