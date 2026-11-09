# SDD ledger — plan: docs/refactor-plan.md

## Etapele autorizate — 2026-11-09

Mediu verificat: `<project-root>`, hostname `Raspberry Pi test host`,
`uname -m`: `aarch64`, `/proc/device-tree/model`:
`Raspberry Pi 5 Model B Rev 1.1`. Compilatoare instalate: Clang 19.1.7,
GCC 14.2.0, target Linux ARM64. Nicio dependență instalată.

### Finalizarea etapei 1

`git status`, `git diff` și indexul au confirmat numai modificările așteptate:
`tests/test_api.py`, `docs/stage-1-results.md` și backupul existent
`backups/before-stage-1.tar.gz`. Indexul era gol. Nu existau alte modificări.

Au trecut `make clean && make CC=clang && make test` și
`make clean && make CC=gcc && make test`: 14/14 teste pentru fiecare build,
fără diagnostice. `git diff --check` fără erori.

Commit local executat: `5036294`, `test: add HTTP characterization tests`.
Conține exclusiv `tests/test_api.py` și `docs/stage-1-results.md`.
Raportul etapei 1 rămâne fotografia situației de atunci, înainte de acest commit.
Metadatele Git sunt read-only în sandbox; commitul a fost executat cu escaladare
autorizată. Nu s-a executat push și nu s-a configurat remote.

## Etapa 2 — fișiere

| Fișier | Modificare |
| --- | --- |
| `src/http_response.h` | Interfețele `http_respond`, `http_json_reply`, `http_error_reply`, statusuri/reason/MIME denumite și contractul de ownership. |
| `src/http_response.c` | Trimiterea completă, formatarea headerului, răspunsuri JSON și maparea erorilor cu fallback 500; helpers private. |
| `src/main.c` | Eliminarea funcțiilor extrase, includerea headerului și substituții mecanice ale apelurilor/constantelor de răspuns. |
| `Makefile` | Liste explicite de surse/headere și ținta testului C, inclusă în `make test`. |
| `tests/test_api.py` | Helper raw-socket limitat, timeout finit, test 204 fără body până la EOF și răspuns 411. |
| `tests/test_http_response.c` | Opt cazuri de caracterizare pentru transport, formatare și maparea erorilor. |
| `docs/stage-2-results.md` | Acest raport/ledger. |

API-urile modulului păstrează semnăturile `void` din plan. Descriptorul și
bufferele sunt împrumutate. Modulul nu închide conexiunea; politica SIGPIPE și
cleanup-ul rămân în serverul existent. Metadatele sunt interne, de încredere;
body trebuie să ofere numărul de octeți declarat.

Bufferul headerului rămâne 512 octeți, acum constantă denumită. Rezultatul
negativ `snprintf` este verificat înaintea conversiei la `size_t`; truncarea
nu trimite header sau body. Trimiterile parțiale avansează doar cu numărul de
octeți returnat de `send`; EINTR reia operația, iar zero/alte erori încheie
încercarea curentă. Formatarea, ordinea headerelor, MIME pentru 204 și toate
mesajele sunt păstrate.

## Caracterizare înainte de refactorizare

- `make test` pe implementarea veche: 16/16 teste de integrare OK, inclusiv cele
  două adăugate înainte de mutarea codului.
- Testul 204 citește direct socketul până la EOF și compară întregul răspuns:
  status, headere, Content-Length 0 și niciun octet după separatorul headerelor.
  Nu folosește interpretarea specială 204 din `http.client`.
- Harness-ul C a fost scris înaintea implementării modulului. A fost compilat
  cu Clang și rulat împotriva blocului original de funcții din `main.c`, copiat
  automat într-un director temporar, modificând doar numele publice și
  vizibilitatea `static`. Rezultat: 8 cazuri OK.
- Interceptarea la link a `send` și `snprintf` permite condiții deterministe,
  fără hooks în producție: EINTR urmat de trimiteri de 3 octeți, zero, EPIPE,
  rezultat negativ de formatare, rezultat egal cu capacitatea, overflow real
  prin MIME prea mare, răspuns fără body, mapări 400/404/405/411/413/500 și
  fallback pentru cod necunoscut. Sunt opt grupuri de cazuri, nu opt teste Python.

Nu a fost introdus comportament nou: testele de caracterizare au trecut pe
implementarea existentă înainte de extragere. Nu s-a fabricat un ciclu RED
pentru o schimbare pur structurală; nu a fost necesară o corecție de bug.

## Verificare după extragere

1. `make clean && make CC=clang && make test`: build server Clang, 16/16 teste
   de integrare OK, 8 cazuri C OK; fără diagnostice.
2. `make clean && make CC=gcc && make test`: build server GCC, 16/16 teste
   de integrare OK, 8 cazuri C OK; fără diagnostice.
3. Makefile definește implicit CC=gcc: în comanda de la punctul 1, al doilea
   proces make construiește harness-ul C cu GCC. Pentru verificare explicită
   și cu Clang s-a executat suplimentar
   `make CC=clang -B build/test_http_response && ./build/test_http_response`:
   8 cazuri C OK. La final serverul este GCC, harness-ul C este Clang.
4. `git diff --check`: fără erori. Nu s-au modificat flagurile stricte C17.
5. Comparație automatizată cu `HEAD:src/main.c`: după inversarea redenumirilor
   și constantelor, fișierul este identic cu originalul fără blocul extras.
   Confirmă că parserul, deciziile routerului, storage și CLI nu s-au schimbat.
   `web/index.html` este identic octet cu octet cu versiunea comisă.

Testele de integrare au fost executate autorizat în afara sandboxului pentru
socketurile localhost. Harness-ul C nu deschide socketuri și rulează în sandbox.
Nicio afirmație privind unelte de complexitate: nu s-a rulat o asemenea unealtă.
Review manual: funcțiile modulului sunt scurte, cu puține ramuri și o singură
responsabilitate; nu s-au fragmentat artificial pentru o limită de linii.

Task 2: complete (base 5036294, changes uncommitted; Clang/GCC integration
16/16, response harness 8 cases OK with both compilers, diff check clean).

## Review și decizii

Reviewer independent read-only prin `requesting-code-review`: fără probleme
critice, importante sau minore care să necesite modificări. A verificat codul,
Makefile și testele, dar nu a rulat independent suita și nu a revizuit acest
raport, creat după verdict. Rezultatele de execuție provin din agentul principal.

- Ruling: se păstrează comportamentul original `void` la eșec de transport,
  inclusiv încercarea separată a body-ului după un header netrimis. Schimbarea
  propagării erorilor ar depăși refactorizarea fidelă. Cost: apelantul nu poate
  afla dacă s-a trimis întregul răspuns; testele fixează explicit această limită.
- Ruling: parserul, limitele storage, politicile JSON/CLI și literalii de rutare
  rămân pentru etapele ulterioare; doar metadatele răspunsurilor sunt centralizate.
  Cost: complexitatea și riscurile existente în celelalte componente rămân.
- Ruling: se lucrează în checkoutul și ramura existente, conform cererii; fără
  worktree suplimentar, commit automat al etapei 2 sau cleanup prin ștergere.
  Acest raport ține loc de ledger persistent. Cost: modificările etapei 2
  rămân vizibile și necomise în checkoutul curent.
- Limitări de test: link wrappers GNU pentru harness, disponibile pe acest Linux;
  comportamentele de kernel la întrerupere/backpressure nu sunt forțate prin
  semnale reale. Integrarea folosește serverul și socketuri reale pentru API.
- Nu s-au observat probleme noi de producție. Resetul posibil la upload prea
  mare, documentat în etapa 1, nu este schimbat de extragerea răspunsurilor.

Următorul pas propus: etapa 3, task storage independent. Nu a fost începută.
Backupul existent este păstrat necomis. Nu s-a făcut push.
