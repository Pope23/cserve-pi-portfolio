# SDD ledger — plan: docs/refactor-plan.md

## Mediu și finalizarea etapei 5 — 2026-11-09

Verificat: `Pope23`, `aarch64`, Raspberry Pi 5 Model B Rev 1.1,
`<project-root>`. Diff-ul și fișierele noi inițiale aparțineau
exclusiv etapei 5. Backupurile sunt păstrate și ignorate prin /backups/.

Observația minoră din review a fost corectată prin documentarea invariantului
`*pos < cap` lângă append, fără schimbare de comportament. La verificarea după
staging a fost detectată și eliminată o linie goală EOF din router.c; motivul
este consemnat și în raportul etapei 5. Nu a fost un defect de producție.

Înainte de commit au trecut build-urile curate și suitele cu CC explicit Clang,
respectiv GCC: 32 metode Python și 20 grupuri C fiecare. S-a repetat configurația
Clang ASan/UBSan din raportul precedent, cu oprire la eroare: aceeași suită OK.
Diff check în index curat. Fără warnings de compilare.

Commit local al etapei 5: **aea0b06**, `refactor: extract router and JSON helpers`.
Conține explicit Makefile, main.c, router.c/h, test_api.py și stage-5-results.md.
Nu s-a folosit git add -A, nu s-au inclus backupuri și nu s-a executat push.

## Etapa 6 — arhitectură și limite

Scopul a fost rezumat înaintea implementării: separarea CLI de ciclul de viață
al serverului, păstrând exact interfețele planificate:

```c
struct ServerConfig { const char *host; uint16_t port; };
int server_run(const struct ServerConfig *config);
```

Main construiește configurația implicită, parsează/validează host/port, emite
aceleași mesaje CLI și returnează rezultatul server_run. Limitele portului,
opțiunile CLI, valorile implicite și codurile de ieșire sunt denumite.
Politica lexicală strtol existentă este păstrată, fără validare suplimentară.

Serverul deține listenerul, fiecare client acceptat și un singur context storage
pe durata apelului. Configurația și șirul host sunt împrumutate, validate de
apelant. Helpers private separă timeouturile, tratarea clientului, bind/listen,
deschiderea listenerului și bucla accept. Dependențe: POSIX IPv4 sockets,
http_read_request, router_handle, http_error_reply și task_storage_init.

Păstrate: default 127.0.0.1:8080, LAN numai prin opțiune explicită, backlog 16,
timeout client 5 secunde pentru recv/send, SO_REUSEADDR, SIGPIPE ignorat,
accept EINTR reluat, o cerere per conexiune, cod 1 pentru erorile serverului și
cod 2 pentru CLI invalid. Nu s-a adăugat protocol de oprire, threading sau DB.

Riscurile identificate înaintea schimbărilor: descriptor pierdut sau închis de
două ori, stare storage reinițializată pentru fiecare client, schimbarea
diagnosticelor, pierderea politicii SIGPIPE/EINTR și expunerea implicită în LAN.

## Fișiere schimbate în etapa 6

| Fișier | Modificare |
| --- | --- |
| `src/server.h` | ServerConfig/server_run, defaulturi și exit codes, contractul ownership. |
| `src/server.c` | Listener, bind/listen, accept, timeouturi, client și context storage. |
| `src/main.c` | CLI și apel server_run, cu constante denumite și helpers mici. |
| `Makefile` | Noul modul și harness C lifecycle, integrat în make test. |
| `tests/test_api.py` | Patru teste noi și verificarea explicită a diagnosticelor sanitizer din stderr. |
| `tests/test_server.c` | Șase scenarii deterministe pentru erori POSIX și cleanup. |
| `docs/stage-6-results.md` | Acest raport/ledger. |

Comparația octet cu octet față de HEAD confirmă că router.c/h, http_parser.c/h,
http_response.c/h, task_storage.c/h și web/index.html nu au fost modificate.
Endpointurile, JSON-ul, headerele, limitele și interfața web sunt păstrate.

## Caracterizare și verificarea resurselor

Înainte de refactorizare au trecut 36 metode Python și 20 grupuri C pe serverul
existent. Cele patru metode Python noi acoperă:

1. Opțiune necunoscută, argument lipsă, port 0/65536, host nepermis -> exit 2.
2. Port ocupat ținut de un socket real -> exit 1 și diagnostic bind/listen.
3. Client închis după header parțial, apoi health funcțional și proces activ.
4. După 32 cereri health, /proc/PID/fd nu indică acumularea descriptorilor;
   se tolerează cel mult un client aflat încă în închidere la momentul inspecției.

După extragere, toate cele 36 și cele 20 grupuri existente au trecut din nou.
Harness-ul C suplimentar folosește server.c și storage reale, cu interceptare
POSIX la link și dubluri pentru parser/router/error reply. Verifică șase grupuri:
socket eșuat, bind eșuat, listen eșuat, accept întrerupt urmat de clienți și eroare
fatală, erori de parser, respectiv setsockopt eșuat conform politicii ignorate.
Verifică și SIGPIPE ignorat, backlog/timeouturi exacte, storage comun între
doi clienți și fiecare descriptor închis exact o dată. Pe fiecare client nou
Request este inițializat la zero. Listenerul nu se închide dacă socket a eșuat;
se închide la eșec bind/listen și după ieșirea buclei accept.

Mesajele `Too many open files`, `Address already in use` și `Input/output error`
din acest harness sunt rezultate intenționate ale injectării, nu eșecuri ale
suitei. Nu se pretinde că aceste erori au fost provocate în kernelul real.

## Rezultatele finale reale

| Verificare | Rezultat |
| --- | --- |
| `make clean && make CC=clang && make test` | 36/36 Python, HTTP C 8, storage C 6, parser C 6, server C 6; toate OK. |
| Rebuild explicit Clang al celor patru harness-uri și executarea lor separată | 26 grupuri C OK. |
| `make clean && make CC=gcc && make test` | Aceleași 36/36 Python și 26 grupuri C, toate OK. |
| Clang ASan/UBSan, toate binarele instrumentate | 36/36 Python și 26 grupuri C, toate OK. |
| `git diff --check` | Fără erori. |

Comanda suplimentară pentru harness-uri a fost:

```sh
make CC=clang -B build/test_http_response build/test_task_storage build/test_http_parser build/test_server
./build/test_http_response
./build/test_task_storage
./build/test_http_parser
./build/test_server
```

Este necesară deoarece make test fără CC construiește implicit harness-urile
cu GCC, chiar dacă serverul a fost construit anterior cu Clang. Cele 36 sunt
metode unittest, fără a număra subtestele; cele 26 sunt grupuri în patru binare C.
Nu au existat warnings cu flagurile stricte C17. Artefactele finale sunt GCC.

### Sanitizers și leak-uri

S-a refolosit configurația din rapoartele anterioare, aplicată și noului harness:

```sh
make clean
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 make CC=clang CFLAGS='-std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -D_POSIX_C_SOURCE=200809L -fsanitize=address,undefined -fno-omit-frame-pointer' test
```

Rezultat: 36/36 Python, 26 grupuri C, exit 0. Testele inspectează acum stderr-ul
serverului pentru `Sanitizer` și `runtime error:`, inclusiv la startup eșuat,
pentru ca un exit sanitizer 1 să nu fie confundat cu exitul așteptat pe port ocupat.
Nicio eroare de memorie/UB raportată pe cazurile exercitate.

`ASAN_OPTIONS=help=1 ./build/test_server` pe binarul instrumentat a confirmat
`detect_leaks: Current Value: true`. Harness-urile și procesele de startup eșuat
ies normal și nu au raportat leak-uri. Pentru serverul real pornit continuu,
testele folosesc SIGTERM: LeakSanitizer nu dovedește absența tuturor leak-urilor
pe un shutdown normal inexistent. Nici ASan, nici LSan nu înlocuiesc verificarea
descriptorilor: pentru aceștia s-au folosit /proc și scenariile C de cleanup.
Nu s-a introdus oprire grațioasă doar pentru a extinde verificarea.

Testele localhost au rulat autorizat în afara sandboxului. Harness-urile C nu
cer socketuri reale. Nu s-au instalat dependențe.

Task 6: complete (base aea0b06, changes uncommitted; Clang/GCC Python 36/36,
C groups 26/26, ASan/UBSan suite OK, diff check clean).

## Erori, review și decizii

Niciun defect de producție nou sau eșec al suitei în etapa 6. Testele de
caracterizare au trecut înainte și după mutare; nu s-a fabricat un RED pentru
extragerea structurală și nu s-a introdus comportament nou.

Review independent read-only: fără probleme blocante. Reviewerul a compilat și
rulat separat cele șase cazuri C server cu GCC și flagurile stricte; nu a rerulat
integrarea sau sanitizers și nu a revizuit acest raport, redactat după verdict.
Diff check a fost verificat și de reviewer.

- Ruling: se păstrează comportamentul existent pentru SIGPIPE, eșecurile
  setsockopt/send și close, fără retry/propagare nouă. Cost: aceste limitări
  rămân; schimbarea lor necesită un scop separat și teste de comportament.
- Ruling: fără graceful shutdown, conform planului. Cost: acoperirea leak-urilor
  unui server real pe termen lung și la shutdown normal rămâne incompletă.
- Ruling: documentația de arhitectură generală și auditul global rămân în etapa 7.
  Cost: README/design descriu încă structura anterioară până la acea etapă.
- Ruling: checkout și ramură existente, cu acest raport ca ledger persistent;
  fără worktree nou, commit al etapei 6 sau ștergere de backupuri. Cost: schimbările
  rămân vizibile și necomise în checkoutul curent.
- Complexitatea a fost revizuită manual, fără instrument și fără scoruri măsurate.
  Funcțiile server/CLI sunt scurte și au responsabilități distincte; nu au fost
  fragmentate doar pentru numărul de linii.

## Recomandări pentru etapa 7

Actualizează README și docs/design cu modulele implementate, efectuează auditul
global al constantelor/duplicării/ramurilor și repetă verificările finale ale
planului. Dacă se măsoară complexitatea cu un instrument disponibil, consemnează
comanda, versiunea și convenția. Păstrează separate propunerile de schimbare
de comportament și limitările de shutdown/transport. Etapa 7 nu a fost începută.

## Git status final

```text
 M Makefile
 M src/main.c
 M tests/test_api.py
?? docs/stage-6-results.md
?? src/server.c
?? src/server.h
?? tests/test_server.c
```

Index gol. Ultimul commit: aea0b06 (etapa 5). Etapa 6 nu este comisă.
Backupurile sunt păstrate și ignorate. Niciun push.
