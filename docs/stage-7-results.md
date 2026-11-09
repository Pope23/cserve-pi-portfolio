# SDD ledger — plan: docs/refactor-plan.md

## Mediu și finalizarea etapei 6 — 2026-11-09

Director `<project-root>`, hostname **Raspberry Pi test host**, arhitectură **aarch64**,
model **Raspberry Pi 5 Model B Rev 1.1**. /etc/os-release identifică Debian 13
trixie; /etc/rpi-issue indică imagine Raspberry Pi din 2026-04-21, generată cu
pi-gen. Testarea este nativă pe această placă. Compilatoare: Clang 19.1.7 și
GCC 14.2.0. Nu se extrapolează rezultatele la alte sisteme.

Diff-ul complet și fișierele noi inițiale au fost examinate: exclusiv etapa 6.
Clang, GCC și configurația ASan/UBSan existentă au trecut fiecare 36 metode
Python și 26 grupuri C. Fără warnings sau erori de diff. Fișierele noi au fost
verificate și pentru whitespace/linie goală EOF. /backups/ este ignorat și
arhiva existentă este păstrată.

Commit local al etapei 6: **cc3f571**, `refactor: extract server lifecycle module`.
Doar Makefile, main.c, server.c/h, test_api.py, test_server.c și stage-6-results.md.
Fără git add -A, modificarea istoricului existent sau push.

## Scopul etapei 7 și arhitectura finală

S-au executat auditul, documentarea și verificările finale din plan. Nu s-au
adăugat funcționalități sau modificări de comportament în producție. Toate
fișierele src/ și web/index.html sunt identice octet cu octet cu HEAD cc3f571.
Testele noi caracterizează comportamentul existent, fără un RED artificial.
O eventuală schimbare de comportament ar cere RED → GREEN → REFACTOR separat.

```text
main: CLI și configurație validată
  -> server: listener, clienți, timeouturi, ownership TaskStorage
       -> http_parser: Request și citire/framing limitat
       -> router: rute, handlers, pagina fixă, JSON privat
            -> task_storage: array fix, create/delete/at, ID-uri
            -> http_response: headere, JSON/errors, send complet
       -> http_response: erori de parsare
```

Interfețele finale rămân cele din plan: ServerConfig/server_run,
Request/http_read_request, router_handle, funcțiile http_respond/json_reply/
error_reply și TaskStorage/init/create/delete/at. Storage este independent de
HTTP/JSON/socketuri; routerul împrumută fd-ul; serverul deține și închide fd-urile.
Helpers private nu sunt expuse pentru testare.

### Fișiere schimbate în etapa 7

- README.md: structura reală, rezultate native, comanda sanitizer pentru toate
  binarele și limitările operaționale.
- docs/design.md: arhitectura modulară, ownership, limite, semnale și dovezi.
- tests/test_api.py: cinci metode de audit pentru SIGTERM/SIGINT și timeout;
  fără schimbarea implementării serverului.
- docs/stage-7-results.md: acest raport/ledger.

## Testele reale și reproducerea verificărilor

Suita inițială avea 36 metode Python și 26 grupuri C. Cele cinci metode noi
verifică SIGTERM și SIGINT cu/fără client incomplet, plus timeout de recepție.
Total final: **41 metode unittest și 26 grupuri C** (8 HTTP response, 6 storage,
6 HTTP parser, 6 server lifecycle). Subtestele și aserțiunile nu sunt numărate
ca teste separate.

| Comandă/verificare executată | Rezultat |
| --- | --- |
| `make debug && make test` | 41/41 Python + 26 grupuri C OK; server GCC ASan/UBSan, harness-uri normale. |
| Suita Clang ASan/UBSan complet instrumentată, comanda de mai jos | 41/41 Python + 26 grupuri C OK. |
| `make clean && make CC=clang && make test` | 41/41 Python + 26 grupuri C OK. |
| Rebuild explicit și executare separată a celor patru harness-uri cu Clang | 26/26 grupuri C OK. |
| `make clean && make CC=gcc && make test` | 41/41 Python + 26 grupuri C OK. |
| `git diff --check`, inclusiv verificarea fișierului nou | Fără erori. |

make test fără CC folosește implicit GCC la build-ul harness-urilor. De aceea,
după build-ul serverului Clang s-au executat și:

```sh
make CC=clang -B build/test_http_response build/test_task_storage build/test_http_parser build/test_server
./build/test_http_response
./build/test_task_storage
./build/test_http_parser
./build/test_server
```

Configurația complet instrumentată, refolosită din etapa 6:

```sh
make clean
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 make CC=clang CFLAGS='-std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -D_POSIX_C_SOURCE=200809L -fsanitize=address,undefined -fno-omit-frame-pointer' test
```

Nu au apărut warnings sau diagnostice sanitizer pe aceste cazuri. Mesajele errno
din harness-ul lifecycle sunt injectate intenționat și sunt așteptate. Testele
Python inspectează și stderr-ul serverului pentru diagnostice sanitizer.
Logurile complete locale sunt în /tmp/cserve-stage7-{debug,sanitizers,clang,gcc}.log.
Artefactele finale din build sunt GCC fără sanitizers. Socketurile de integrare
au rulat autorizat în afara sandboxului; nu s-au instalat dependențe.

Clang Static Analyzer 19.1.7 a fost executat pe toate cele șase unități C:

```sh
clang --analyze -std=c17 -D_POSIX_C_SOURCE=200809L -Xanalyzer -analyzer-output=text src/main.c src/server.c src/http_parser.c src/http_response.c src/router.c src/task_storage.c
```

Exit 0, fără diagnostice. Acest rezultat nu este o certificare de securitate sau
o măsurătoare de complexitate ciclomatică.

## Audit tehnic și securitate

| Zonă | Dovezi și rezultat | Limită rămasă |
| --- | --- | --- |
| Descriptori | Close exact o dată în harness pentru socket/bind/listen/accept/parser; /proc/PID/fd stabil după 32 cereri reale; rebind după semnale. | Erorile close sunt ignorate; scenariile injectate nu reproduc toate condițiile kernel. |
| Timeouturi | Configurare recv/send 5 secunde verificată în C; cerere incompletă expiră în test și health rămâne funcțional. | Sunt timeouturi per apel, fără deadline total; setsockopt eșuat este ignorat. Clientul lent poate monopoliza serverul secvențial. |
| Întreruperi | EINTR la accept/recv/send și trimiteri parțiale în harness-uri; SIGPIPE ignorat. | Erorile de transport nu sunt propagate către apelant; body-ul are încercare separată după eșecul headerului, ca înainte. |
| Buffere | Header 8192, body 4096, lungimi sscanf sincronizate, overflow Content-Length respins; titlu 120 octeți și round-trip cu escaping maxim. | Contractele interne cer argumente valide; nu sunt API-uri pentru pointeri arbitrar nevalizi. |
| HTTP malformat | Duplicate/negativ/text/overflow Content-Length, TE, header mare, body NUL, EOF prematur, rute/metode invalide sunt testate. | Tokenizare CR/LF tolerantă, fără Host obligatoriu sau conformitate HTTP/1.1 completă; octeții suplimentari sunt ignorați. |
| Storage | 128 taskuri, FULL înainte de ID_EXHAUSTED, INT32_MAX refuzat, create fără mutare pe eșec, pointer created păstrat, contexte independente. | Datele se pierd la restart; storage este mutat înainte de serializarea răspunsului. |
| Dashboard | web/index.html identic cu versiunea comisă; GET / funcțional; textContent pentru datele taskurilor. | Fără sesiune browser/validare vizuală; maxlength HTML nu exprimă limita UTF-8 în octeți. |
| Memorie și semnale | ASan/UBSan verde; startup-uri/harness-uri cu exit normal fără raport de leak; SIGTERM/SIGINT caracterizate mai jos. | Absența tuturor leak-urilor serverului pe termen lung nu este demonstrată. |
| Acces | Default loopback, host nepermis respins, pagina are cale fixă, fără includere de input în format strings. | Fără auth/TLS: nepotrivit pentru expunere publică pe Internet. |

Calcul independent al capacității JSON: un task cu ID maxim alocabil și 120
octeți care necesită escaping dublu ocupă cel mult **268 octeți**, sub bufferul
de creare de 368. 128 asemenea taskuri, separatorii și containerul ocupă cel
mult **34443 octeți**, sub 65536. Calculul folosește lungimile formatului real
și factorul maxim de escaping, nu presupune un titlu de 120 caractere Unicode.

UTF-8 primit direct nu este validat complet și poate produce text JSON
neconform. Limita în octeți și surrogate escapes sunt testate; validarea
strictă suplimentară ar schimba acceptarea inputului și nu a fost implementată.

## SIGTERM/SIGINT și cauza limitării LSan

Cele patru teste reale au observat returncode **-15** la SIGTERM și **-2** la
SIGINT, atât pentru serverul pornit normal, cât și cu un client acceptat care
nu a completat body-ul. Procesul este reaped și portul poate fi legat din nou.
Sursele instalează numai politica SIGPIPE; nu există handler de oprire pentru
SIGTERM/SIGINT. Kernelul eliberează descriptorii, dar aplicația nu drenează
cererea și nu ajunge la cleanup-ul normal după bucla accept.

Investigația separată a folosit un program temporar, în /tmp, cu o pierdere
intenționată de 4096 octeți, nu un defect introdus în CServe Pi. Sursa de control:

```c
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
static void leak(void) {
    volatile char *data = malloc(4096);
    if (!data) _Exit(2);
    data[0] = 1;
}
int main(int argc, char **argv) {
    (void)argv;
    leak();
    puts("ready");
    fflush(stdout);
    if (argc > 1) for (;;) pause();
    return 0;
}
```

Compilat cu Clang -std=c17 -O0 -g -Wall -Wextra -Wpedantic -Werror
-fsanitize=address,undefined. Cu ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 și
LSAN_OPTIONS=exitcode=23, controlul pozitiv cu exit normal a produs **exit 23**
și raport LeakSanitizer pentru **4096 bytes**. Același executabil, după mesajul
ready, oprit prin SIGTERM/SIGINT, a produs -15/-2 și **niciun raport LSan**, deși
leak-ul intenționat exista în toate cazurile.

Experimentul confirmă de ce absența raportului după semnal nu dovedește lipsa
leak-urilor: calea de terminare prin semnal ocolește verificarea normală de exit.
Nu indică un leak demonstrat în CServe Pi. Oprirea grațioasă, drenarea clienților
și un exit normal controlat necesită schimbări de comportament. Sunt raportate
separat, fără implementare automată în etapa 7.

## Clean Code și review independent

Au fost revizuite responsabilitățile, dependențele, constantele și ramurile din
toate modulele. Nu s-a găsit duplicare nouă de logică; append_task este comun,
lungimile de protocol sunt derivate din constante, iar stocarea nu depinde de
transport. Helpers de encoding păstrează blocuri coerente, uneori peste circa
20 linii, fără fragmentare artificială.

lizard, pmccabe și clang-tidy nu sunt disponibile în PATH. Nu s-a instalat o
unealtă de complexitate. Ramurile au fost revizuite manual pentru fiecare
funcție; reviewerul nu a identificat necesitatea unei descompuneri pentru limita
maximă. **Nu se raportează scoruri măsurate și pragul numeric nu este certificat
automat**. Unele helpers JSON rămân mai ramificate decât ținta sub 10.

Reviewer independent read-only: a citit toate modulele/headerele, dashboardul,
testele noi, README și design; fără constatări critice sau importante în scopul
etapei 7. A inspectat finaluri de log debug/sanitizers/GCC, fără să reruleze testele;
Clang nu era încă terminat la inspecția sa. Agentul principal a confirmat ulterior
exit 0 și 41/41 și în logul Clang. Acest raport este redactat după verdict și a
fost verificat de agentul principal, nu independent.

- Final: minor (deferred): testul de timeout impune o limită superioară de 7
  secunde, fără aserțiune de timp minim; un 400 imediat l-ar satisface. Harness-ul
  C verifică exact configurarea de 5 secunde. Se poate întări testul de timp într-o
  schimbare ulterioară, fără a altera implementarea aici.
- Final: minor (deferred): maxlength=120 din frontend măsoară unități de șir,
  nu octeți UTF-8. Limitarea existentă este acum documentată; UX-ul nu este schimbat.
- Referința README la raportul încă absent în momentul review-ului este rezolvată
  prin crearea și verificarea acestui document.
- Ruling: politicile de validare HTTP/UTF-8, timeout total și erorile de transport
  rămân neschimbate. Cost: riscurile educaționale descrise persistă; hardening-ul
  necesită un scop separat, teste de comportament și autorizare.
- Ruling: fără graceful shutdown. Cost: lipsa unei verificări complete LSan pe
  ieșirea normală a serverului real și întreruperea cererilor la semnale persistă.
- Ruling: checkoutul existent și raportul ca ledger persistent, fără worktree
  nou sau commit al etapei 7. Cost: rezultatele/documentele rămân necomise aici.

Task 7: complete (base cc3f571, changes uncommitted; Python 41/41, C groups
26/26 on debug/full sanitizers/Clang/GCC; static analyzer clean; diff check clean).

## Publicare GitHub și CI — recomandări, neimplementate

Proiectul este potrivit tehnic ca **portofoliu educațional localhost/LAN**, cu
limitările declarate; nu este server de producție pentru Internet. Înainte de
publicare: revizuiește și comite separat etapa 7 și decide licența (nu există
LICENSE/COPYING în proiect). Nu a fost ales automat un regim de licențiere.

CI nu este implementat. Recomandare: joburi Linux GCC/Clang cu flagurile stricte,
suita completă, job Clang ASan/UBSan care instrumentează toate binarele și job
nativ ARM64 când este disponibil. Păstrează logurile și distinge probele pe
hardware real de emulare/cross-build. Nu se propun versiuni de actions neverificate
și nu s-a creat un workflow sau configurat remote.

Pentru review înainte de publicare: inspectează și istoricul pentru date private,
exclude artefactele build/backups, prezintă limitările de shutdown și HTTP și
efectuează o verificare vizuală a dashboardului. Nu se revendică un audit complet
de secrete al istoricului sau certificare de securitate.

## Git status final

```text
 M README.md
 M docs/design.md
 M tests/test_api.py
?? docs/stage-7-results.md
```

Index gol; ultimul commit este cc3f571 (etapa 6). Istoricul nu a fost rescris.
Etapa 7 nu este comisă, backupurile nu au fost șterse, nu s-a făcut push și
nu au fost începute lucrări suplimentare.
