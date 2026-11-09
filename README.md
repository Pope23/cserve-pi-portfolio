# CServe Pi — lightweight HTTP task server in C17

Un mic server HTTP/1.1 în C17, compilat nativ pe **Raspberry Pi OS / Linux**.
Realizează conexiuni TCP cu POSIX sockets și oferă un dashboard HTML + REST API.
Nu necesită framework-uri, librării C externe sau Docker.

> **Important:** proiect educațional / portofoliu. Nu este un server HTTP de producție.
> Nu are autentificare sau HTTPS; nu redirecționa portul 8080 din router către Internet.

## Funcționalități

- `GET /` — interfață web pentru taskuri.
- `GET /health` — diagnostic `{"status":"ok"}`.
- `GET /api/tasks` — listă `{"tasks":[{"id":1,"title":"..."}]}`.
- `POST /api/tasks` — adaugă un task cu `{"title":"Învață C"}` (201).
- `DELETE /api/tasks/1` — șterge taskul cu ID 1 (204).
- Validare de bază a datelor și coduri HTTP (400, 404, 405, 411, 413, 507).
- Teste de integrare în Python standard library.
- Configurare `--host` și `--port`; maximum 128 taskuri în memorie.

**Limitări asumate:** taskurile se pierd la repornire, serverul deservește o conexiune
la un moment dat, suportă un subset al HTTP/1.1 (fără keep-alive, chunked uploads,
TLS, server public sau autentificare). JSON-ul de creare acceptă un singur câmp
`title`, un șir UTF-8 de maximum 120 **octeți**.

## 1. Pe Raspberry Pi — instalează uneltele

Ai nevoie de Raspberry Pi cu Raspberry Pi OS deja instalat, conectat în aceeași
rețea cu laptopul. Deschide terminalul pe Pi sau conectează-te de pe Windows:

```powershell
ssh NUME_UTILIZATOR@IP_RASPBERRY_PI
```

Pe Raspberry Pi, rulează:

```bash
sudo apt update
sudo apt install -y build-essential python3 curl unzip git
```

`make`, `gcc` și Python sunt folosite pentru build și teste.
Nu e necesar WSL/Visual Studio pe placa Raspberry Pi.

## 2. Transferă arhiva de pe Windows pe Pi

Descarcă `cserve-pi.zip` din conversație în folderul Downloads. În **PowerShell**:

```powershell
scp "$env:USERPROFILE\Downloads\cserve-pi.zip" NUME_UTILIZATOR@IP_RASPBERRY_PI:~/
```

Apoi, în terminalul Raspberry Pi:

```bash
unzip ~/cserve-pi.zip -d ~
cd ~/cserve-pi
```

Alternativ, după ce proiectul este publicat pe GitHub, îl poți clona direct:

```bash
git clone URL_REPOSITORY_TAU.git ~/cserve-pi
cd ~/cserve-pi
```

## 3. Compilează și testează pe placa Raspberry Pi

```bash
make
make test
```

La final ar trebui să apară `OK`. Testele pornesc automat câte un server
local temporar și verifică HTTP, JSON, Unicode, operațiile CRUD și erorile.

Pentru o compilare cu sanitizers pe Linux, dacă sunt disponibile:

```bash
make debug
make test
```

`make debug` instrumentează serverul cu ASan/UBSan. Pentru a instrumenta și
toate harness-urile C, configurația verificată pe Pi este:

```bash
make clean
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 make CC=clang CFLAGS='-std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -D_POSIX_C_SOURCE=200809L -fsanitize=address,undefined -fno-omit-frame-pointer' test
```

Pentru a reveni la binarul normal: `make clean && make`.

## 4. Pornește serverul

Doar pe Raspberry Pi (acces local):

```bash
./build/cserve
```

Verifică într-un al doilea terminal de pe Pi:

```bash
curl http://127.0.0.1:8080/health
```

Pentru acces de pe laptop în **aceeași rețea locală**, oprește serverul
cu `Ctrl+C` și repornește-l astfel:

```bash
./build/cserve --host 0.0.0.0 --port 8080
```

Află IP-ul plăcii:

```bash
hostname -I
```

Pe laptop deschide în browser `http://IP_RASPBERRY_PI:8080/`, de exemplu
`http://192.168.1.50:8080/` **doar dacă acesta este IP-ul real al plăcii**.

## 5. Testează endpointurile manual

Într-un alt terminal Raspberry Pi, cât timp serverul rulează:

```bash
curl http://127.0.0.1:8080/health
curl http://127.0.0.1:8080/api/tasks
curl -i -X POST http://127.0.0.1:8080/api/tasks \
  -H 'Content-Type: application/json' \
  --data '{"title":"Test pe Raspberry Pi"}'
curl http://127.0.0.1:8080/api/tasks
curl -i -X DELETE http://127.0.0.1:8080/api/tasks/1
```

## 6. Opțional: pornire automată cu systemd (user service)

Acest pas presupune că proiectul este în `~/cserve-pi` și că dorești să îl
accesezi pe rețeaua locală. Este util pentru un Pi fără monitor:

```bash
mkdir -p ~/.config/systemd/user
cp deploy/cserve.service ~/.config/systemd/user/cserve.service
systemctl --user daemon-reload
systemctl --user enable --now cserve.service
systemctl --user status cserve.service
```

Pentru ca serviciul utilizatorului să poată porni și după reboot, chiar
fără autentificare SSH, poți activa *linger*:

```bash
sudo loginctl enable-linger "$USER"
```

Jurnal: `journalctl --user -u cserve.service -f`

Oprire: `systemctl --user stop cserve.service`

**Nu porni serverul manual în același timp cu serviciul:** ambele vor
încerca să folosească portul 8080 și vei primi `Address already in use`.

## 7. Superpowers — agentul de programare

Superpowers (`https://github.com/obra/superpowers`) este o metodologie pentru
agenții de cod, **nu** o dependență a serverului C. Instalează pluginul în
mediul tău de cod, de exemplu Cursor Agent: `/add-plugin superpowers`, sau
Codex CLI: `/plugins` > caută `superpowers` > `Install Plugin`.

Deschide folderul `cserve-pi` cu agentul (local sau prin VS Code Remote SSH),
iar agentul va putea citi `AGENTS.md`, `docs/design.md` și `docs/roadmap.md`.
Poți începe cu promptul din `docs/prompt-superpowers.md`.

## Structură

```text
cserve-pi/
  src/main.c                 CLI și configurația validată
  src/server.c / .h          listener, clienți, timeouturi și context storage
  src/http_parser.c / .h     parsare HTTP și citire limitată a cererii
  src/router.c / .h          rute, handlers, JSON și pagina fixă
  src/http_response.c / .h   formatarea și trimiterea răspunsurilor
  src/task_storage.c / .h    array în memorie, ID-uri și operații cu taskuri
  web/index.html             dashboard în HTML/JS
  tests/test_api.py          teste de integrare
  tests/test_http_response.c teste C pentru send/formatare/erori
  tests/test_http_parser.c   teste C pentru recv/parsare/limite
  tests/test_task_storage.c  teste C pentru capacitate/ID-uri/izolare
  tests/test_server.c        teste C pentru ciclul de viață și cleanup
  deploy/cserve.service      user service systemd, opțional
  docs/design.md             arhitectura modulară și contractele
  docs/refactor-plan.md      cele șapte etape de refactorizare
  docs/stage-7-results.md    auditul și rezultatele finale
  docs/roadmap.md            idei de evoluție testabile
  docs/prompt-superpowers.md prompt pentru agent AI
  AGENTS.md                  reguli de lucru
  Makefile                   build și teste
  .gitignore
```

## Testare și compatibilitate

Build/test nativ verificat pe **Pope23, Raspberry Pi 5 Model B Rev 1.1,
aarch64**, Debian 13 (trixie), imagine Raspberry Pi generată prin pi-gen.
Clang 19.1.7 și GCC 14.2.0 compilează fără warnings cu flagurile stricte.
Suita finală conține **41 metode Python și 26 grupuri C**; subtestele și
aserțiunile nu sunt numărate separat. Nu sunt revendicate teste pe alte plăci
sau arhitecturi; rulează `make test` și pe sistemul tău.
Nu include un executabil ARM precompilat.

SIGTERM și SIGINT opresc procesul prin acțiunea implicită a semnalului; kernelul
eliberează descriptorii, dar aplicația nu drenează cererile și nu parcurge un
cleanup grațios. LeakSanitizer nu execută verificarea normală de ieșire pe
această cale. Un rezultat fără raport LSan după semnal **nu demonstrează lipsa
leak-urilor**. Verificările și limitările sunt în `docs/stage-7-results.md`.

Timeouturile de 5 secunde se aplică apelurilor recv/send, fără deadline total
al cererii. Serverul rămâne secvențial, fără autentificare/TLS, cu un subset HTTP
și fără validare completă a UTF-8 primit direct. Este potrivit pentru demonstrații
locale și portofoliu, nu pentru expunere publică pe Internet.

## Licență

Adaugă o licență pentru repository când decizi cum vrei să distribui codul.
