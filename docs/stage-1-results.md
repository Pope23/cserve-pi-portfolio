# SDD ledger — plan: docs/refactor-plan.md

## Etapa 1 — rezultate, 2026-11-09

- Mediu: `<project-root>`, hostname `Pope23`, `aarch64`,
  `/proc/device-tree/model`: Raspberry Pi 5 Model B Rev 1.1.
- Backup înainte de modificări: `backups/before-stage-1.tar.gz`. Verificat prin
  compararea octeților arhivați cu fișierele originale; nu conține directoarele
  mediului `.aws`, `.codex`, `.agents` sau metadate Git.
- Git inițial absent, confirmat și în afara sandboxului. Bază locală verificată:
  `d6b82de` (`chore: record verified CServe Pi baseline`), cu cele 13 fișiere
  originale. Identitate Git configurată doar local, după răspunsul utilizatorului.
- Ramură: `test/stage-1-contract`. Modificările etapei 1 rămân necomise.
  Nu s-a configurat remote și nu s-a executat push.

## Execuție și verificări

- Pre-flight: no shared interfaces — executată exclusiv etapa 1.
- Bază: Clang build și 9/9 teste OK, înainte de modificarea testelor.
- Adăugate 5 teste și un helper de aserțiuni; health verifică și bytes JSON.
  Acoperire: metadate GET/POST/DELETE/erori, lungime în octeți cu Unicode,
  MIME exact, JSON exact pentru health și erorile 400/404/405/413,
  ID-uri DELETE invalide și diferențele metodă/rută cerute.
- Prima rulare GCC a expus `ConnectionResetError` la citirea body-ului 413
  după upload supradimensionat. Serverul existent poate închide cu octeți
  necitiți în socket. Fixture-ul nou declară Content-Length 5000 fără payload;
  verifică astfel răspunsul la lungimea respinsă. Testul original care trimite
  efectiv body mare rămâne nemodificat. Nu s-a modificat serverul.
- Verificare finală: `make clean && make CC=clang && make test` — 14/14 OK;
  `make clean && make CC=gcc && make test` — 14/14 OK. Fără diagnostice compiler.
  Binarul rămas în `build/` este cel compilat cu GCC.
- Testele necesită rulare autorizată în afara sandboxului; în sandbox,
  socketurile sunt blocate cu EPERM. Aceasta nu este o eroare a serverului.
- Comparația cu backupul confirmă că dintre fișierele originale s-a modificat
  doar `tests/test_api.py`; C, Makefile, interfața web și regulile sunt identice.
- `git diff --check`: fără erori.

Task 1: complete (base d6b82de, changes uncommitted; Clang 14/14, GCC 14/14).

## Decizii de execuție

- Ruling: instrucțiunea curentă autorizează doar etapa 1 și commitul de bază;
  restul planului și commitul modificărilor nu sunt executate. Cost dacă greșit:
  necesită o comandă ulterioară pentru continuare sau commit.
- Ruling: se lucrează în directorul explicit cerut, pe ramura dedicată, cu
  backup verificat; nu se creează alt worktree. Ledgerul persistă aici, fără
  ștergeri sau automatizări de skill care ar presupune commituri suplimentare.
  Cost dacă greșit: modificările de teste sunt vizibile în checkoutul curent.
- Ruling: teste de caracterizare, fără RED artificial sau implementare nouă,
  conform cerinței utilizatorului. Fixture-ul 413 este stabilizat fără a repara
  comportamentul serverului la uploads necitite. Cost dacă greșit: problema
  existentă a resetului de conexiune rămâne pentru o etapă separată.

## Review independent

Skill `requesting-code-review`, reviewer separat read-only: fără probleme
critice sau importante. Reviewerul a comparat arhiva cu fișierele și nu a
rulat independent testele; rezultatele de mai sus provin din execuția principală.

- Final: minor (deferred): `http.client` tratează automat 204 ca body de lungime
  zero. Aserțiunile confirmă body-ul expus clientului și Content-Length 0, nu
  absența octeților suplimentari pe fir. Verificare raw-socket la o etapă ulterioară.
- Final: Ruling: parser/framing, capacitate storage, limite titluri și CLI rămân
  în etapele 3–6, conform planului. Cost dacă greșit: acele riscuri nu primesc
  acoperire suplimentară acum.
- Final: Ruling: comportamentul de închidere la upload supradimensionat rămâne
  neschimbat; repararea transportului ar depăși etapa exclusiv de teste.
  Cost dacă greșit: un client poate observa în continuare resetul existent.

Următorul pas propus, neexecutat: etapa 2, extragerea suportului pentru
răspunsuri HTTP, folosind aceste teste ca protecție a contractului.
