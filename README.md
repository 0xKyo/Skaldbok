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

Cada fila es un **módulo** (el menú lateral los lista):

| Módulo | |
|---|---|
| **Search** (siempre activo) | Búsqueda de texto completo sobre criaturas, hechizos, reglas y tablas de los libros y de todos los packs; `Ctrl+K`. |
| **Encounter** | Iniciativa con cartas 1–10, PV, condiciones, notas y tirada de ataque de cada criatura. Se guarda solo. Acepta criaturas y **personajes** (sus PV se mantienen sincronizados con la ficha). |
| **Characters** | Fichas de jugadores y **creador de personajes** paso a paso según el libro (ver más abajo). |
| **Party** | Agrupa personajes que juegan juntos (agregar/quitar, estado de todos de un vistazo, "Add the party to the encounter", *Message the party…*). |
| **Messages** | **Chat real** con cada jugador (ellos escriben desde su página web y vos ves los mensajes al instante, con contador de no leídos) y **broadcast** a una party o a todos, que a los jugadores les llega con un estilo propio. Texto e imágenes en las dos direcciones. Los jugadores solo hablan con el máster, nunca entre ellos. |
| **Master Screen** | Un lienzo infinito personal del máster: fijás criaturas, hechizos, tablas, personajes, la party, notas e imágenes, y los movés, agrandás y cerrás; varios tableros con pestañas (ver más abajo). |
| **Web server** *(engranaje de arriba a la derecha)* | El servidor web de los jugadores, **apagado hasta que lo arrancás** con el botón *Start server* (o con la casilla *Launch server when opening*). Un botón *Web server: on/off* en la barra de arriba muestra si está funcionando y abre esta pestaña; ahí ves la dirección en tu red y el enlace personal de cada personaje (copiar / enviarlo en un mensaje). |
| **Creatures** | Criaturas de los tres libros y del homebrew: ilustración, statblock, tabla de *Monster Attacks* con botón de tirada, habilidades, encuentro aleatorio y semilla de aventura. Si hay algo que explicar sobre qué son los monstruos y cómo funcionan (el capítulo Bestiary del libro), aparece como pestaña **Intro** aparte de la pestaña con la lista. |
| **Spells · Abilities · Skills · Kin · Professions** | Igual que Creatures: una lista filtrable a la izquierda y la entrada elegida a la derecha, con todos sus datos (requisitos, alcance, texto entero, sus tablas, enlace al original). Si la categoría trae su explicación general (magia y sus escuelas, cómo tirar los dados, aptitudes heroicas...), aparece como pestaña **Intro** aparte. Abilities tiene **Generate Ability** y **Edit** (nombre, Heroic o Innate, requisito, Willpower, descripción); Kin tiene **Generate Kin** (nombre, movimiento, descripción, nombres e imagen, y sus aptitudes innatas de una lista, con opción de crear una nueva ahí mismo) y **Edit** (la del libro incluida: tu versión la reemplaza en todos lados, sin tocar el original). Generate y Edit guardan en el mismo pack propio de homebrew, `custom` (guardar de nuevo actualiza la tarjeta, no agrega otra). **Delete** borra una que creaste; en una edición de una tarjeta del libro o de otro pack, **Revert to original** deshace tu edición. |
| **Gear** | Armas, armaduras y equipo: no tienen lista propia, son las tablas del libro (Armor & Helmets, Melee/Ranged Weapons, Trade Goods...) en una sola página, con qué significa cada columna. |
| **Character Creation · Combat & Damage · Adventures** | Los capítulos del libro con página propia, en Reference: un árbol de secciones, filtro por texto (también por título de tabla) y enlace a la página original, **con sus tablas dentro de la sección a la que corresponden** (las de dado con botón *Roll* que resalta el resultado, y *Pin* para el Master Screen). **Character Creation** reúne los pasos del libro para crear un personaje, numerados y con enlaces (*See also*) a Kin, Professions, Skills... de donde salen las opciones. **Adventures** es la aventura *The Misty Vale* completa (capítulos, ubicaciones numeradas y sus tablas). Vienen del pack Core, de su `rules.json` (ver más abajo): un capítulo con `"nav"` tiene su página. |
| **Rules** | Aparece solo si un pack trae **homerules** o tablas que no pertenecen a un capítulo con página propia; sin ellas no hay entrada. |

Además: **dados** en la barra inferior (D4–D20 y expresiones como `2D8+3`), botón *Original: libro p.N* en cada ficha
(abre tu propio PDF en su página, en el lector del sistema; la app no muestra ni distribuye páginas del libro), **recientes** en la pantalla de inicio y filtro
de fuentes en la barra superior (cada libro, y un menú *Homebrew* con una entrada por fuente).

El ícono del engranaje es el de *settings* de [Material Icons](https://github.com/google/material-design-icons) (Apache 2.0; licencia en
`assets/icons/`), un PNG blanco con transparencia (`tools/make_icons.py` lo genera y lo incrusta en la app), teñido según el estado.

Atajos: `Ctrl+K` buscar · `Alt+←/→` atrás y adelante · `↑/↓` moverse por una lista · `Ctrl +/−/0` tamaño del texto.

## Contenido y homebrew

Todo el contenido —reglas, criaturas, hechizos, aptitudes, habilidades, razas, profesiones, equipo, tablas— vive en
**packs**: una carpeta (o un `.zip`) con **un JSON por tipo** (`rules.json`, `creatures.json`, `spells.json`, …), más `images/` para el
arte. El `manifest.json` es **opcional**: sin él, el `id` es el nombre de la carpeta y el nombre, los libros y demás datos del pack van
en la cabecera de su primer JSON (los de Core están en su `manifest.json`). El contenido de los libros es un solo pack incorporado,
**Core** (`data/packs/core`), que se abre primero y es la base de los demás: solo tiene las criaturas, hechizos, razas, profesiones...
Una categoría que crece mucho puede pasar a su propio JSON. Lo que no es un elemento de una categoría va aparte, en `data/system/`:
`rules.json` (las reglas genéricas y sus tablas, la aventura incluida) y el `intro` de cada página (`spells.json`, `creatures.json`...);
la app los muestra en el mismo lugar de siempre.

* **Homerules**: un pack de homerules es un pack común con un `rules.json`, escrito como un **árbol anidado** (cada regla lista sus
  `"children"`). Sus reglas pueden colgar de una regla de Core (`"parent"`) o **reemplazarla** (`"replaces"`): la regla
  conserva su lugar en el árbol, muestra la fuente *Homebrew · <nombre>* y
  dice *"Changed by <pack>"*, así que se ve siempre qué es del libro y qué es de la casa. Apagar o quitar el pack la devuelve a como era.
* **Importar**: el engranaje › **General Settings › Content packs** › *Import a pack folder…* o *Import a .zip…* (o `skaldbok --import <carpeta o .zip>` desde una terminal). Un pack que se importa lleva su `manifest.json`, porque el `id` no se puede deducir de una carpeta temporal. El pack se valida antes de instalarse
  (si algo está mal, se rechaza con el archivo y la posición del error) y se copia a la carpeta de packs del usuario (`%APPDATA%\skaldbok\gm\packs\` en Windows). También alcanza con copiar la carpeta ahí: la app la toma sola.
* Lo importado **se suma** al Core y **conserva su fuente**: *Homebrew · <nombre>*, con su color, su filtro y su etiqueta en cada tarjeta.
* Cada pack se puede **apagar** sin borrarlo (la casilla de su fila), **actualizar** (importar otra vez con el mismo `id`) o **quitar** (*Remove*).
* Las criaturas de un pack se incluyen en la búsqueda, el encuentro y el creador de personajes; una raza, profesión, habilidad
  o hechizo homebrew se puede elegir al crear un personaje.
* **En vivo**: la app vigila los archivos de los packs (Core y los importados). Si editás un JSON, el `manifest.json` o las imágenes de un pack
  mientras la app está abierta, lo relee sola en un segundo y avisa ("Content reloaded", o el error del archivo si quedó mal escrito).
* `pack_check <carpeta|zip>` valida un pack desde una terminal con el mismo código de la app.

Formato completo, con todos los campos de cada tipo: [`docs/HOMEBREW.md`](docs/HOMEBREW.md). Un ejemplo listo para importar:
[`docs/examples/frostmarch-tales/`](docs/examples/frostmarch-tales).

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

Lo normal es **abrir `Skaldbok.bat`** y, cuando quieras que entren los jugadores, apretar **Start server** en **el engranaje › Web server**
(o el botón *Web server: off* de la barra de arriba; con la casilla *Launch server when opening* arranca con la app); los jugadores, en la misma
Wi-Fi, abren su enlace (`http://<ip-de-tu-pc>:8080/?t=<token>`, que se copia desde esa pestaña). La página se compila una vez con
Node (`cd web && npm install && npm run build`; el `.bat` lo hace si falta). También se puede correr a mano, sin la app:

```
build\release\skaldbok_web.exe --public-url http://mi-direccion
```

## Cómo se arma

```
data/packs/core/    (pack Core, la base: reglas y tablas de los libros, la aventura, criaturas, hechizos, razas... + images/)
                          ▼
   skaldbok  (C++ / SDL3 / Dear ImGui)  ◄── packs de homebrew y de homerules (carpeta del usuario)
```

1. **Datos**: `data/packs/core` (la app ya no lee ninguna base de datos). Salen de tu copia de los libros; `tools/`
   guarda los scripts de Python con los que se extrajeron por primera vez (ver `tools/README.md`).
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

La app busca `data/` (con `packs/core`) junto al ejecutable, uno a tres niveles arriba, en `$SKALDBOK_DATA` o
con `--data <carpeta>`. Los archivos del usuario (ajustes, recientes, encuentro, personajes, packs importados) van a
`%APPDATA%\skaldbok\gm\` (`--prefs <carpeta>` para cambiarlo).

## Liviana

Todo el contenido se lee una sola vez al arrancar desde JSON (unos 2 MB entre criaturas, hechizos, etc.); las imágenes se
decodifican bajo demanda con una caché de 12, la base de los libros se abre en solo lectura (`immutable`), la app no redibuja
mientras nada cambia. Con `--software` usa el renderizador por software de SDL.

## Estructura

```
                ── núcleo (libgm_core): sin UI y sin servidor; lo usan todas las vistas ──
src/parsing/    lee el JSON y lo convierte al modelo interno: content (packs), packs (importar), jsonutil, jsondir (stores
                por usuario que releen lo que otro programa escribe), fsutil (archivos), fts/sql (índice de búsqueda)
src/game/       la lógica del juego sobre ese modelo: model (datos), character, creation (reglas), party, encounter, dice,
                messages (chat), sheet_edit (edición por campos y revisión de reglas), changelog, master_screen (datos del
                lienzo), settings, web_link
                ── vistas: cada una enlaza gm_core y nada más; ninguna incluye de otra ──
src/ui/         la app del GM (ImGui): main, app.cpp (el shell: navegación, historial, filtro de fuentes, dados), module.h
                (el contrato Module / Host y los servicios opcionales entre módulos), ui_common, fonts, textures, filedialog,
                homebrew_forms (Generate / Edit de Kin y Abilities)
src/ui/modules/ un archivo por módulo (search, master_screen, encounter, characters + character_sheet, party, messages,
                catalog, gear, rules, web). catalog: la página lista + detalle (con su Intro) de cada tipo
                de contenido que tenga página propia según la tabla de tipos de game/model.cpp; creatures agrega su detalle
src/web/        el servidor de jugadores: app sin sockets (web_app), vistas JSON (web_views), tokens (web_access), HTTP (web_server)
web/client/     la página de los jugadores (Vue 3 + Vite); se compila con npm y la sirve skaldbok_web
tests/          ctest sin ventana: contenido y packs, personajes y parties, encuentro, Master Screen, chat, edición de hojas (dos escritores sobre un archivo)
                y la web de jugadores (autenticación, privacidad, HTTP real); las pruebas de la página están en web/client (npm test)
tools/          conversor PDF → SQLite + pack Core en Python, y pack_check
docs/           HOMEBREW.md (formato de packs), CHARACTERS.md (personajes y parties), WEB.md (web de jugadores)
docs/examples/  frostmarch-tales: un pack de ejemplo, solo como documentación (la app no lo usa)
scripts/        build.ps1, package.ps1 (Windows)
```

**Añadir una vista** (una API, otra UI): un target nuevo que enlace `gm_core`; todo lo que necesita (leer packs, personajes,
reglas) ya está ahí. Lo que haga falta compartir entre vistas va al núcleo, nunca de una vista a otra.

**Añadir un módulo**: una clase que hereda de `Module` en `src/ui/modules/`, su fábrica en `modules.h` y una línea en
`registry.cpp`. Aparece sola en el menú. Un módulo solo habla con los demás mediante `Host` y los
servicios opcionales (`serviceOf<IEncounterSink>(host)` devuelve nulo si ningún módulo lo ofrece, y el resto lo tolera).

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
* Los packs de contenido homebrew (`docs/examples/`) son originales y se publican bajo la misma licencia.
