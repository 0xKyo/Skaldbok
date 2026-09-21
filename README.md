[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/T2M427FAPU)

# Skaldbok

**Skaldbok** (de *skald*, el bardo nórdico, y *bok*, libro: el libro del narrador) es una herramienta libre para dirigir partidas de
**Dragonbane**. Reúne el Rulebook, el Bestiary y la aventura *The Misty Vale* y los
muestra en una app liviana (C++23, Dear ImGui sobre SDL3), con la paleta de la hoja de personaje oficial (papel crema, pergamino,
verde dragón y rojo). Cada dato indica de qué libro salió y en qué página, con acceso
directo al original. Es **modular** (cada parte se activa o se apaga) y admite **contenido homebrew** que se importa como
un plugin de datos.

> Uso personal: los PDF originales tienen copyright y no forman parte del repositorio (`References/` y `data/` están en `.gitignore`).

## Qué tiene

Cada fila es un **módulo** (Settings › Modules los enciende y apaga; el menú lateral solo muestra los encendidos):

| Módulo | |
|---|---|
| **Search** (siempre activo) | Búsqueda de texto completo sobre criaturas, reglas, tablas de los libros y todos los packs; `Ctrl+K`. |
| **Encounter** | Iniciativa con cartas 1–10, PV, condiciones, notas y tirada de ataque de cada criatura. Se guarda solo. Acepta criaturas y **personajes** (sus PV se mantienen sincronizados con la ficha). |
| **Characters** | Fichas de jugadores y **creador de personajes** paso a paso según el libro (ver más abajo). |
| **Party** | Agrupa personajes que juegan juntos (agregar/quitar, estado de todos de un vistazo, "Add the party to the encounter", *Message the party…*). |
| **Messages** | **Chat real** con cada jugador (ellos escriben desde su página web y vos ves los mensajes al instante, con contador de no leídos) y **broadcast** a una party o a todos, que a los jugadores les llega con un estilo propio. Texto e imágenes en las dos direcciones. Los jugadores solo hablan con el máster, nunca entre ellos. |
| **Master Screen** | Un lienzo infinito personal del máster: fijás criaturas, hechizos, tablas, personajes, la party, notas e imágenes, y los movés, agrandás y cerrás; varios tableros con pestañas (ver más abajo). |
| **Web** *(en Settings)* | Arranca solo, junto con la app, el servidor web de los jugadores; muestra su estado, la dirección en tu red y el enlace personal de cada personaje (copiar / enviarlo en un mensaje). |
| **Creatures** | Criaturas de los tres libros y del homebrew: ilustración, statblock, tabla de *Monster Attacks* con botón de tirada, habilidades, encuentro aleatorio y semilla de aventura. |
| **Spells · Abilities · Skills · Kin · Professions · Equipment** | Mosaicos de tarjetas con todos los datos (requisitos, alcance, texto entero, enlace al original). Todas las tarjetas de un mosaico tienen el mismo tamaño; si un texto no entra se recorta y, al hacer clic, esa tarjeta crece hacia abajo sin reacomodar el resto. Filtros por texto y por escuela/tipo/categoría (las escuelas o categorías que traiga un pack aparecen solas). |
| **Tables** | 104 tablas del libro más las de los packs; las de dado tienen botón *Roll* que resalta el resultado. |
| **Rules** | Lo que queda del texto del Rulebook y el Bestiary cuando criaturas, hechizos, habilidades, razas, profesiones y tablas ya tienen su página (sin la aventura, el índice ni el contenido). Árbol de capítulos, filtro por texto y enlace a la página original: sirve para revisar qué se queda y qué se va. |
| **Settings** (siempre activo) | Se abre con el **engranaje** de arriba a la derecha (otra vez el engranaje, o `Esc`, vuelve). Pestañas: **General** (módulos, packs de contenido: importar/apagar/quitar homebrew, dónde se guarda cada cosa) y **Web**. |

Además: **dados** en la barra inferior (D4–D20 y expresiones como `2D8+3`), botón *Original: libro p.N* en cada ficha
(con las páginas prerenderizadas se ve dentro de la app; si no, abre el PDF), **recientes** en la pantalla de inicio y filtro
de fuentes en la barra superior (cada libro, y un menú *Homebrew* con una entrada por fuente).

El ícono del engranaje es el de *settings* de [Material Icons](https://github.com/google/material-design-icons) (Apache 2.0; licencia en
`assets/icons/`), un PNG blanco con transparencia (`tools/make_icons.py` lo genera y lo incrusta en la app), teñido según el estado.

Atajos: `Ctrl+K` buscar · `Alt+←/→` atrás y adelante · `↑/↓` moverse por una lista · `Ctrl +/−/0` tamaño del texto · `Esc` cierra el visor.

## Contenido y homebrew

Todo lo que no son las reglas base —criaturas, hechizos, aptitudes, habilidades, razas, profesiones, equipo, tablas— vive en
**packs**: una carpeta (o un `.zip`) con un `manifest.json` y **un JSON por tipo** (`creatures.json`, `spells.json`, …) más
`images/` para el arte. El contenido de los libros es el pack **Core** (`data/packs/core`, generado desde los PDF) y se carga
exactamente igual que cualquier homebrew.

* **Importar**: Settings › Content packs › *Import a pack folder…* o *Import a .zip…*. El pack se valida antes de instalarse
  (si algo está mal, se rechaza con el archivo y la posición del error) y se copia a la carpeta del usuario.
* Lo importado **se suma** al Core y **conserva su fuente**: *Homebrew · <nombre>*, con su color, su filtro y su etiqueta en cada tarjeta.
* Cada pack se puede **apagar** sin borrarlo, **actualizar** (importar otra vez con el mismo `id`) o **quitar**.
* Las criaturas de un pack se incluyen en la búsqueda, el encuentro y el creador de personajes; una raza, profesión, habilidad
  o hechizo homebrew se puede elegir al crear un personaje.
* `pack_check <carpeta|zip>` valida un pack desde una terminal con el mismo código de la app.

Formato completo, con todos los campos de cada tipo: [`docs/HOMEBREW.md`](docs/HOMEBREW.md). Un ejemplo listo para importar:
[`examples/frostmarch-tales/`](examples/frostmarch-tales).

## Personajes

**Characters** guarda cada personaje como un JSON (`docs/CHARACTERS.md`: formato pensado para que una futura web app de los
jugadores lo lea) y trae un creador que sigue el capítulo 2 del Rulebook:

1. raza (elegir o tirar D12) · 2. profesión (elegir o tirar D10; el mago elige su escuela de magia) · 3. edad (joven / adulto /
viejo, elegir o D6) · 4. atributos (4D6 quitando el más bajo, asignados a medida que se tiran, intercambio final de dos, o a
mano) · 5. habilidades entrenadas (6 de la profesión, el resto libres; nivel = doble de la probabilidad base) · 6. aptitud heroica
o, para el mago, 3 hechizos y 3 trucos · 7. equipo inicial (uno de los tres conjuntos; los dados se tiran) · 8. nombre, apodo,
debilidad, recuerdo y apariencia (con las tablas del libro) · 9. revisión.

Todo lo elegible sale del contenido cargado (Core + packs activos). Después la ficha se edita libremente: atributos, PV/PW,
condiciones, habilidades (nivel, marca de avance, tirada de D20 con dragón/demonio, tirada de avance de fin de sesión),
aptitudes, hechizos, equipo (armas a mano, armadura, casco, mochila, monedas), notas, exportar/importar/duplicar. **Random**
crea un personaje completo de una vez. Desde la ficha: *Add to encounter*, *Message…*, *Pin* (al Master Screen) y exportar/duplicar.

La ficha está armada como la **primera hoja (la verde) de la hoja de personaje oficial**, pensada para PC: nombre en el pergamino, las seis
gemas de atributos con su condición debajo (clic en el rombo para activarla), bonos de daño y movimiento, habilidades en dos columnas
(rombo = marca de avance; clic en el nombre = entrenar; clic en *(ATRIBUTO)* = tirar un D20), habilidades y hechizos, inventario numerado,
monedas, recuerdo y objetos diminutos, armadura, casco, armas y los círculos de PV y PW (clic en un círculo para fijar el valor). Los
números se editan donde están impresos: clic en una gema para escribir, rueda del mouse para subir o bajar.

## Master Screen

Un **lienzo infinito** solo para el máster, con lo que quieras tener a mano como referencia:

* **Fijar**: el botón *Pin* de una criatura, un hechizo/aptitud/habilidad/equipo (en sus tarjetas), una tabla, un
  personaje o una party; el botón *Pin…* de la barra (busca en todo); *+ Note*, *+ Image…* o arrastrar un archivo de imagen a la ventana.
* **Mover y ordenar**: arrastrá la barra de título para mover, la esquina para agrandar, doble clic para plegar, × para cerrar (*Undo remove*
  o `Ctrl+Z` lo devuelve), clic derecho en el título para color, duplicar, traer al frente / mandar al fondo, abrirlo en su sección o mandar
  la imagen a los jugadores. La rueda hace zoom sobre el puntero; arrastrar el fondo (o el botón del medio) desplaza. *Fit* encuadra todo.
* Lo que se fija es una **referencia** (se guarda la clave del contenido, no una copia): un personaje muestra sus PV actuales (y se pueden
  cambiar ahí mismo), una criatura sigue al pack de donde salió; si el pack se quita, el ítem lo dice.
* **Tableros** con pestañas (clic derecho en la pestaña: renombrar o borrar), candado para no mover nada por accidente.
* Se guarda solo, **por usuario**, en `master_screen.json` de la carpeta del usuario (escritura atómica; un archivo dañado se aparta como
  `.damaged` en vez de perderse).

## Chat y broadcast

El módulo **Messages** es un chat de verdad, sobre la misma web que ya usan los jugadores. Cada personaje tiene su conversación con el máster
(un jugador nunca ve la de otro ni puede escribirle): a la izquierda la lista, ordenada por actividad y con los no leídos; a la derecha la
conversación, con imágenes y con "seen" cuando el jugador leyó. Cuando un jugador escribe, la app avisa ("Aria: …") aunque estés en otro módulo.
**Broadcast…** manda lo mismo a una party o a todos: en cada conversación aparece como un cartel destacado (borde rojo y "BROADCAST"),
distinto de un mensaje común, y las respuestas de los jugadores vuelven a vos, de a uno.

Cada mensaje es su propio archivo (`chat/<personaje>/<id>.json`, imágenes en `chat/media/`) y cada lado guarda solo hasta dónde leyó, así que el
máster y el servidor pueden escribir a la vez sin pisarse. Desde una criatura, *Send this picture to the players…* abre el formulario con su arte.

## Los jugadores editan su hoja

En la web, cada jugador puede **editar todo** su personaje (menos raza, profesión y escuela, que se eligen al crearlo en tu app): atributos,
PV/PW y sus máximos, condiciones, habilidades (nivel, entrenada, marca de avance), aptitudes y hechizos, armas, armadura, inventario, monedas,
textos. Lo que cambia se sincroniza solo, campo por campo, y vos lo ves **en vivo** aunque estés mirando esa hoja (la app relee los archivos ~3
veces por segundo y muestra un aviso: "Aria changed: HP 10 → 7").

* **Las reglas no bloquean, marcan.** Si el jugador se pasa (más carga de la que puede, un atributo fuera de 3–18, PV sobre el máximo, un máximo
  cambiado sin la aptitud que lo permite, una habilidad sobre 18), se le permite pero le aparece **en rojo**. En tu hoja ves un cartel *RULE CHECK*
  con **Validate** (pasa a ser normal) o **Reject** (sigue en rojo: el jugador lo tiene que cambiar a mano). Una aprobación vale para exactamente lo
  aprobado: si agrega otra cosa más, vuelve a preguntar.
* **Registro y deshacer**: en la hoja, *Changes made by the player* lista qué cambió y su valor anterior, con **Undo**.
* **Candado**: *Lock this sheet* impide que ese jugador edite (sigue viendo su hoja y usando el chat).
* Si los dos cambian el mismo campo a la vez, gana el último; si cambian campos distintos, se suman los dos cambios.

## Web para jugadores

`skaldbok_web` es un servidor aparte (C++, sin ventana, mismo núcleo que la app) que lee los archivos del máster y muestra a
cada jugador, en su teléfono y con **su enlace personal**, su ficha completa, el estado de su party y las reglas. Se actualiza
solo cada pocos segundos, permite editar la hoja y chatear con el máster, y no muestra criaturas, notas del máster ni datos de otros jugadores. La página es
Vue 3 (`web/client`). Los enlaces salen con `skaldbok_web --links` y también en la ficha de cada personaje en la app.
Detalles, seguridad, cómo exponerlo por HTTPS y la API: [`docs/WEB.md`](docs/WEB.md).

Lo normal es **abrir `Skaldbok.bat`**: abre la app y ella arranca sola el servidor (módulo **Web**); los jugadores, en la misma
Wi-Fi, abren su enlace (`http://<ip-de-tu-pc>:8080/?t=<token>`, que se copia desde el módulo Web). La página se compila una vez con
Node (`cd web && npm install && npm run build`; el `.bat` lo hace si falta). También se puede correr a mano, sin la app:

```
build\release\skaldbok_web.exe --public-url http://mi-direccion
```

## Cómo se arma

```
References/*.pdf ──► tools/build_db.py ──► data/skaldbok.db        (texto de los libros, tablas, páginas, índice)
                                   └─► data/packs/core/          (pack Core: JSON por tipo + images/)
                       (opcional) tools/render_pages.py ──► data/pages/
                                          ▼
                  skaldbok  (C++ / SDL3 / Dear ImGui)  ◄── packs de homebrew importados (carpeta del usuario)
```

1. **Datos** (una sola vez; Python 3 con PyMuPDF):
   ```
   pip install -r tools/requirements.txt
   python tools/build_db.py          # genera data/skaldbok.db y el pack data/packs/core
   python tools/validate.py          # informe de consistencia
   python tools/render_pages.py      # opcional: páginas para el visor interno (~65 MB)
   ```
2. **App en Windows** (Visual Studio con el workload de C++):
   ```
   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Test
   build\release\skaldbok.exe
   ```
3. **App en Linux**:
   ```
   sudo apt install build-essential cmake ninja-build pkg-config libx11-dev libxext-dev libxcursor-dev libxi-dev \
        libxrandr-dev libxfixes-dev libxss-dev libxkbcommon-dev libwayland-dev libdrm-dev libgbm-dev \
        libgl1-mesa-dev libegl1-mesa-dev libdbus-1-dev libudev-dev
   cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/release
   ctest --test-dir build/release && build/release/skaldbok
   ```
   *(Solo se probó en Windows; el código no usa nada específico de plataforma. Necesita un compilador con C++23.)*

CMake descarga SDL3, Dear ImGui, SQLite, stb_image, nlohmann/json, miniz (zip) y cpp-httplib (servidor web) como archivos fuente y los compila
estáticamente: el ejecutable es autónomo. `scripts\build.ps1 -Headless` compila solo el núcleo y los tests, sin ventana.

La app busca `data/skaldbok.db` (y `data/packs/core`) junto al ejecutable, uno a tres niveles arriba, en `$SKALDBOK_DATA` o
con `--data <carpeta>`. Los archivos del usuario (ajustes, recientes, encuentro, personajes, packs importados) van a
`%APPDATA%\skaldbok\gm\` (`--prefs <carpeta>` para cambiarlo).

## Liviana

Todo el contenido se lee una sola vez al arrancar desde JSON (unos 2 MB entre criaturas, hechizos, etc.); las imágenes se
decodifican bajo demanda con una caché de 12, la base de los libros se abre en solo lectura (`immutable`), la app no redibuja
mientras nada cambia y los mosaicos miden el texto de sus tarjetas una sola vez por ancho de ventana. Los módulos apagados no dibujan ni se consultan. Con `--software` usa el renderizador por software de SDL.

## Estructura

```
src/            núcleo sin ventana (libgm_core): db, content (packs), packs (importar), character, creation (reglas), messages (chat),
                sheet_edit (edición por campos y revisión de reglas), changelog, master_screen (datos del lienzo), settings, encounter, dice;
                fsutil/jsondir (archivos y stores por usuario, que releen lo que otro programa escribe)
src/modules/    un archivo por módulo (search, master_screen, encounter, characters + character_sheet, party, messages, creatures, compendium, tables,
                web, settings)
src/web/        el servidor de jugadores: app sin sockets (web_app), vistas JSON (web_views), tokens (web_access), HTTP (web_server)
web/client/     la página de los jugadores (Vue 3 + Vite); se compila con npm y la sirve skaldbok_web
src/module.h    el contrato Module / Host y los servicios opcionales entre módulos (IEncounterSink, IMessenger, ...)
src/app.cpp     el shell: navegación, historial, filtro de fuentes, dados, visor de páginas
tests/          ctest sin ventana: contenido y packs, personajes y parties, encuentro, Master Screen, chat, edición de hojas (dos escritores sobre un archivo)
                y la web de jugadores (autenticación, privacidad, HTTP real); las pruebas de la página están en web/client (npm test)
tools/          conversor PDF → SQLite + pack Core en Python, y pack_check
docs/           HOMEBREW.md (formato de packs), CHARACTERS.md (personajes y parties), WEB.md (web de jugadores)
examples/       frostmarch-tales: un pack de ejemplo
scripts/        build.ps1, package.ps1 (Windows)
```

**Añadir un módulo**: una clase que hereda de `Module` en `src/modules/`, su fábrica en `modules.h` y una línea en
`registry.cpp`. Aparece sola en el menú y en Settings › Modules. Un módulo solo habla con los demás mediante `Host` y los
servicios opcionales (`serviceOf<IEncounterSink>(host)` devuelve nulo si ese módulo está apagado, y el resto lo tolera).

Opciones de línea de comandos para pruebas: `--shot archivo.png` (captura y sale), `--tab <id de módulo>`, `--select <nombre>`,
`--search <texto>`, `--roll`, `--page N`, `--demo-encounter`, `--demo-character`, `--demo-party`, `--demo-screen`, `--new-character <paso>`, `--import <pack>`,
`--prefs <carpeta>` (aísla ajustes, recientes, personajes y packs), `--size 1200x800`.

## Fiabilidad de los datos

* El texto es literal; los cortes de línea con guion se resuelven usando el vocabulario del propio libro.
* Las tablas con dado se validan contra el dado (las tiradas deben cubrir 1..N) y el conversor cruza lo extraído con el
  índice de cada libro (cantidad de statblocks, tablas de ataques). `tools/validate.py` resume el estado.
* Las capitales ilustradas del Rulebook (9 letras) no tienen texto en el PDF; se leyeron de las páginas renderizadas.
* Los atributos de las habilidades de arma no están en el texto corrido: se tomaron de la hoja de personaje impresa (Rulebook p.127).
* Las referencias "página N" dentro de los textos son páginas **impresas**, igual que en los libros.

## Pendiente / límites conocidos

* Algunas criaturas de la aventura no tienen statblock en el libro: se muestra la referencia ("stats as per page 87 in the
  Rulebook") con enlace a la criatura citada.
* El contenido de la aventura y del Bestiary que no son criaturas (mapas, rumores, eventos) está como texto y tablas, no como entidades.
* Los mensajes viejos de la versión anterior (carpeta `messages/` y `messages-sent.json`, de cuando solo iban del máster al jugador) no se migran; se pueden borrar.
* El Master Screen guarda las imágenes por su ruta en tu disco: si movés el archivo, el ítem avisa que no lo encuentra.

## Licencia

Software libre bajo la **GNU General Public License v3.0 o posterior** (texto completo en [`LICENSE`](LICENSE)): podés usarlo,
estudiarlo, modificarlo y redistribuirlo, y toda versión modificada que distribuyas también tiene que ser libre bajo la misma licencia.

* Dependencias (se descargan al compilar, `cmake/Dependencies.cmake`), todas compatibles con la GPL: Dear ImGui, cpp-httplib,
  nlohmann/json, miniz, stb_image y Vue (MIT), SDL3 (zlib), SQLite (dominio público) e íconos de Material Icons (Apache 2.0, ver
  `assets/icons/`).
* **Dragonbane** es marca y obra de Free League Publishing. Este proyecto no está afiliado a ellos y **no incluye ningún contenido de los
  libros**: `References/` y `data/` (todo lo extraído de los PDF) están en `.gitignore`; cada persona genera esos datos con su propia copia
  legal de los libros (`tools/README.md`).
* Los packs de contenido homebrew (`examples/`) son originales y se publican bajo la misma licencia.
