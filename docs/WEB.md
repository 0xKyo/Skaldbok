# Web: GM y jugadores

El servidor `skaldbok_web` sirve **dos interfaces** desde la misma página Vue:

* **Vista del máster** — se abre con `/?gm=<token>`. Es la interfaz principal del máster desde el browser y se parece a la del jugador: **pestañas arriba** — **Character** (un desplegable con todos los personajes y su ficha), **Chat** (por ahora, el mismo chat del jugador, del personaje elegido), **Rules** (la Reference, idéntica a la de los jugadores). El enlace personal de cada jugador se ve en la cabecera de su ficha (**Player link ↗**) y se imprime con `skaldbok_web --links`.
* **Vista del jugador** — se abre con `/?t=<token>`. Cada jugador ve solo su ficha, su chat con el máster y las reglas; no puede ver ni modificar nada de los demás.

```
skaldbok_web  (C++, sin ventana)  ──JSON──►  web/client (Vue 3, ya compilado)  ──► browser del máster
                                                                                ──► teléfonos de los jugadores
                  │
                  │ lee y escribe
                  ▼
         characters/  packs/  settings.json     (carpeta del usuario)
```

* **`skaldbok_web`** es un ejecutable aparte, hecho con la misma librería que la app de escritorio (`gm_core`): carga los mismos packs, usa las mismas reglas y lee los mismos archivos. No tiene base de datos propia ni lógica duplicada. Puede correr aunque la app de escritorio esté cerrada, incluso en otra máquina con los archivos sincronizados.
* **`web/client`** es una página Vue 3 (Vite). Node solo hace falta para *compilarla*; el servidor sirve los archivos ya compilados (`web/client/dist`, o la carpeta `web/` junto al ejecutable en el paquete).
* **Escritura acotada**: el jugador solo puede cambiar su propia hoja (`PATCH /api/me`) y escribir en su chat; el máster puede editar cualquier ficha y gestionar personajes y el chat vía `/api/gm/*`.

## Ir hacia atrás y hacia adelante

Arriba, junto al nombre, hay dos botones **◀ ▶** (en la página del máster y en la del jugador) para **moverte entre lo que ya viste**, como en un navegador. Cada paso recuerda: la **pestaña** (y el personaje elegido, en la del máster), la **página del Rules** abierta con la entrada de su lista, la **aventura** con su pestaña (*Story* / *NPCs*), la página o el NPC abierto, y lo que está abierto en **Homebrew** (el tipo, y el pack que estás mirando). Ir atrás vuelve a poner todo eso; si después abrís otra cosa, lo que quedaba adelante se descarta. Los botones se apagan cuando no hay adónde ir. El historial vive en la pantalla (hasta 100 pasos): al recargar la página empieza de nuevo. El botón *Atrás* del navegador sigue cambiando las pestañas del jugador como antes.

**Encabezado del máster:** el nombre *Skaldbok* con la versión, las flechas y las pestañas van en **una sola línea** (ya no hay etiqueta "Game Master"); el encabezado queda fijo arriba al hacer scroll. Lo mismo en la página del jugador (sin el resumen del personaje).

## Cómo se ve cada categoría

Todas las páginas de Reference (también Creatures, cuyo texto general del Bestiary es la pestaña **General Info**) tienen la misma estructura, de arriba a abajo: **pestañas** (solo si la entrada las tiene, como Gear o Skills), **intro** (el texto general de la categoría o el texto propio del capítulo, siempre desplegado) y la **lista con la descripción al lado**. No hay título encima. **La búsqueda es una sola, arriba de todo Reference** (ya no hay una por categoría): busca en **todas** las categorías (entradas de cada tipo, criaturas y el texto de los capítulos de reglas) y lista lo que encontró, cada resultado con la categoría en que está; al hacer clic abre esa entrada y deja la búsqueda puesta en esa categoría. Cuando hay una categoría abierta aparece la casilla **Only in <categoría>** (marcada por defecto): marcada, la búsqueda solo filtra esa categoría, como antes; desmarcada, vuelve a buscar en todo. En los capítulos de reglas el filtro se hace en el navegador por título, texto y contenido de las tablas; la respuesta de `GET /api/rules/<cap>` (y `/api/gm/rules/<cap>`) trae el texto de la raíz del capítulo como `intro`, y `GET /api/gm/creatures` trae el `intro` del Bestiary.

Las tablas con dado (las categorías `rollable` de los yaml, o con `dice`) tienen un botón **Roll**: elige un resultado al azar y marca su fila.

**Capítulos por grupos** (`layout: groups`, como **World**, que sale de `world.yaml`): cada regla dice a qué pestaña pertenece con `tab:` (Journeys, NPC, Treasure, Hazards); arriba van las pestañas, debajo el filtro de la búsqueda (que salta a la pestaña que tiene la coincidencia) y la lista de las reglas de esa pestaña con el texto al lado.

**Capítulos con pestañas** (`layout: tabs`, como **Actions**, que sale de `actions.yaml`): cada entrada del capítulo es una pestaña; debajo van su texto y la **lista con las filas de su tabla** (Dash, Melee Attack…), con la información de la fila elegida al lado. La respuesta de `GET /api/rules/<cap>` trae `layout`.

## Creatures

**Reference › Creatures** lista las criaturas con buscador y muestra su ficha completa (estadísticas, ataques, habilidades, texto) **con su ilustración** si la tiene. Arriba de la lista hay tres etiquetas para filtrar, **NPC**, **Animal** y **Monster** (todo lo que no es NPC ni animal); se pueden combinar y, sin ninguna activa, se ve todo. `GET /api/gm/creatures/<key>` trae `image` (la ruta de la imagen, o `null`) y `GET /api/gm/creatures/<key>/image` sirve el archivo; el navegador la pide con el token del máster, igual que las fotos del chat.

## Personajes (máster)

La pestaña **Character** del máster es un desplegable con todos los personajes; **Delete character** (con confirmación) borra su archivo (`DELETE /api/gm/characters/<id>`).

El máster puede cambiar **Kin**, **Profession** y, si es mago, **School** desde la ficha (en edición; el jugador no): van en `kin`, `profession` (`{key, name}`) y `school` del `set`, y solo las acepta `PATCH /api/gm/characters/<id>`. La **edad** es su propia línea. Las tarjetas de armadura traen `slot` (`armor` o `helmet`) y en la ficha, **Armor** y **Helmet** son desplegables con las armaduras y los yelmos de las reglas (más *None*).

En el teléfono (menos de 900 px) la ficha son **cinco pestañas**, para no tener que hacer scroll: **Stats** (atributos, HP y WP; abre ahí), **Skills**, **Abilities**, **Gear** (armas, armadura, yelmo, inventario, monedas, recuerdo y objetos chicos) y **Details** (player, kin, age, appearance, profession, weakness y las notas). El recuadro con el nombre queda siempre arriba. En pantallas anchas se ve todo a la vez, en columnas.
En el teléfono, **Rules** muestra el índice o la categoría elegida, por turnos (**← All categories** vuelve al índice), y una lista con su entrada (Creatures, Kin, etc.) muestra la lista o la entrada, por turnos (**← Back** vuelve a la lista); ninguna entrada se abre sola.

En el inventario de la ficha, cada objeto cuenta para el límite de carga como peso × cantidad. El peso sale del «Weight» de la tarjeta de las reglas (1/4 la ración, — no pesa), se busca por la clave del objeto o, si se escribió a mano, por su nombre; sin tarjeta pesa 1. La web no muestra ni edita el peso; se fija a mano con `weight` en el archivo de la ficha o en el campo de peso de la app de escritorio.

## Creador de personajes (máster)

El botón **+ New character** de la pestaña **Character** abre el creador con los mismos 9 pasos que la app (Kin, Profession, Age, Attributes, Skills, Ability & magic, Gear, Name & details, Review), con sus botones de tirada (D12, D10, D6, 4D6, tablas de weakness/memento/appearance). **Next** se habilita al terminar cada paso; **Create character** en Review guarda la ficha y la abre. **Random character** (dentro del creador) y el botón **Random** generan un personaje completo y válido y lo dejan en Review para retocarlo o crearlo.

Donde se elige una opción (kin, profession, age, heroic ability, gear set) se ve como en la Reference: la lista a la izquierda y a la derecha los datos de la elegida (campos, texto y tablas, las mismas tarjetas de `GET /api/gm/content/<tipo>`). Skills y hechizos, que son de selección múltiple, siguen siendo listas con casillas.

En **Gear**, además de los sets, hay **Custom**: se busca y se agrega cualquier arma, armadura o equipo de las reglas (con cantidad) y se suma lo que cuesta todo (oro, plata y cobre; lo que no tiene un precio simple, como un servicio por día, no se cuenta y se avisa). Va en `customGear: [{key, count}]` de la creación. En **Attributes** lo que se escribe se respeta tal cual: fuera de 3 a 18 se marca en rojo y no deja avanzar.

Las reglas son las de la app (`game/creation.cpp`); la web solo junta las elecciones: `GET /api/gm/creation` (todo lo que ofrece cada paso, con las tablas del libro cara por cara), `POST /api/gm/creation/preview` (problemas, atributos con la edad, HP/WP/movimiento y la hoja en texto), `POST /api/gm/creation/random` (una creación aleatoria) y `POST /api/gm/characters` con `{creation: {...}}` (400 con el primer problema si algo falta). Solo con el token del máster.

## Homebrew (máster)

**Adventure (escribir tus propias aventuras).** La subpestaña **Adventure** es un editor de aventuras. Arriba: el desplegable con tus aventuras, **+ New adventure** (pide el nombre), el **título** (se puede cambiar) y **Delete adventure** (pide confirmación y borra también sus imágenes). Abajo, a la izquierda el **índice** (capítulos y, dentro, secciones a cualquier profundidad, con el nombre que elijas) y a la derecha el editor de lo elegido:

* **+ Chapter** (abajo del índice) agrega un capítulo; **+ Section inside** agrega una sección dentro de lo elegido (y esa, otra dentro, etc.). **↑ ↓** las mueven entre sus hermanas y **Delete** las borra (si tienen texto o cosas adentro, pregunta antes).
* **Name** (el nombre), **Kind** (Section, Location, Sidebar, Map, Text, NPC o Monster; un capítulo no tiene), **Number** (solo en un Location: *1. The Inn*) y **Text** (párrafos; las líneas que empiezan con ✦ salen como lista, igual que en el libro).
* **Pictures**: **+ Picture** sube una imagen (PNG, JPEG o WebP; se achica a 1600 px) que queda dentro de la aventura; se ve en su lugar en la pestaña Adventures y se quita con **Remove**.
* **Creatures**: eliges de un **desplegable** con las criaturas del Reference (agrupadas en NPCs, animales y monstruos) y se crea dentro de la parte elegida una parte nueva de tipo NPC o Monster, con el nombre de la criatura y enlazada a ella (la pestaña Adventures muestra su ficha, como en el libro). Las criaturas enlazadas de una parte se ven como etiquetas con ✕ para desenlazarlas.

Se guarda solo un instante después del último cambio (aparece *Saved*). Cada aventura es **un pack propio** en la carpeta de packs del usuario (`<packs>/<nombre-en-slug>/` con `manifest.yaml`, `adventure.yaml` e `images/uploads/`): la pestaña **Adventures** la lee como las del libro, y entra en el backup. Rutas (solo máster): `GET|POST /api/gm/homebrew/adventures`, `GET|PUT|DELETE /api/gm/homebrew/adventures/<id>` (el árbol `{title, chapters}`; se limpia: solo los campos de una aventura, ids únicos, hasta 2000 partes y 8 niveles) y `POST /api/gm/homebrew/adventures/<id>/images` con `{image: base64}`. Código: `src/web/web_adventure_editor.*`, `HomebrewAdventure.vue`.

**Subpestañas.** Los tipos se agrupan en una fila de subpestañas arriba: **Creatures** (criaturas, weakness, fear), **Characters** (kin, professions, abilities, skills, age, appearance, memento), **Magic** (spells, magic, duration, mishaps), **Equipment** (weapons, armor, gear, improvised weapons), **Combat** (actions, damage types, injuries, melee y ranged demon rolls) **World** (traps, terrain, treasure, hunting) y **Adventure** (el editor de aventuras, abajo). Debajo se ven solo los tipos del grupo abierto; un tipo que no esté en ninguno cae en **More**.

La pestaña **Homebrew**, solo del máster, es donde se hace contenido propio. Arriba, el **nombre del pack** (por defecto **Homebrew**; **Use this name** lo cambia y lo que hagas desde entonces va a una carpeta con ese nombre) y una fila de qué hacer: **Creatures** y un tipo por cada yaml de `data/packs/core` (menos `manifest.yaml`): Abilities, Spells, Skills, Kin, Professions, Weapons, Armor, Gear (los tres de `equipment.yaml`), Traps, Actions y las tablas (Age, Appearance, Damage Types, Fear, Hunting, Improvised Weapons, Injuries, Magic, Melee y Ranged Demon Rolls, Memento, Mishaps, Terrain, Treasure, Weakness).

**Criaturas**: a la izquierda la lista de las que hiciste (**+ New creature**, o **Start from a creature…** para partir de una copia de cualquiera del libro) y a la derecha el formulario: nombre, tipo (monster / npc / animal), categoría, dado de ataque, cita, descripción, **bloques de estadísticas**, **ataques**, **aptitudes**, semilla de aventura y encuentro aleatorio. **Save** la guarda, **Delete** la borra (con confirmación). En el teléfono la lista y el formulario se turnan.

**Los demás tipos** usan un editor común: el servidor dice qué campos tiene cada tipo (texto, texto largo, número, elección, sí/no, lista de textos, columnas etiqueta/valor, filas) y el formulario se arma con eso. En las tablas, una entrada es nombre, categoría (solo si el yaml de Core tiene varias, como Treasure), descripción y columnas. Un nombre es obligatorio; lo que no es campo del tipo se descarta.

Todo se guarda en el pack propio (`<carpeta del usuario>/packs/<nombre>`, se crea la primera vez que se guarda algo), en un archivo con el mismo nombre que el de Core (`spells.yaml`, `equipment.yaml`...), como cualquier pack (ver [`HOMEBREW.md`](HOMEBREW.md)); después de guardar el servidor lo lee de nuevo y aparece enseguida en el Reference con la etiqueta *Source · <nombre del pack>*. Las tablas del homebrew son tablas propias del pack (no se suman a la de Core con el mismo nombre) y se ven en la página *Tables · <pack>* de sus reglas. Kin y Professions todavía no editan sus listas de nombres internas, ni Actions sus grupos. La ilustración de las criaturas todavía no se sube desde la página.

**Campos con ayuda.** En **Spells**: *Requirement* se elige entre Word / Gesture / Material (se pueden marcar 1, 2 o los 3, y para Material se escribe qué hace falta); *Prerequisite* es otro hechizo, una escuela (Animism, Elementalism, Mentalism) o *Any School of Magic*; *Casting time* es Action / Reaction / Shift / Stretch; *Range* es Touch, Personal, o X meters (con sphere o cone si es de área); *Duration* sale de `duration.yaml` (Instant, Round, Stretch, Shift, Permanent, Concentration; lo que agregues en la pestaña **Duration** también aparece ahí). En **Abilities**, *Kin* es un desplegable de todos los kins y solo se ve si *Type* es `kin`. En **Kin** (*Innate abilities*) y **Professions** (*Skills*, *Heroic abilities*) se elige **cualquier cantidad** de las que ya existen; **+ New…** te lleva a la pestaña de ese tipo para crear una, y al guardarla **vuelves** al formulario donde estabas, tal como lo dejaste y con la nueva ya agregada (**Cancel, go back** vuelve sin agregar nada). En **Skills**, *Category* es weapon o magic (vacío: una habilidad normal).

**Mirar los demás packs y hacer house rules.** Arriba, **Looking at** deja ver todos los packs: el tuyo (el único que se puede cambiar), **Core** y cualquier otro de la carpeta de packs del usuario; los demás están marcados 🔒 *read only*. Al abrir una entrada de otro pack ves sus campos sin poder tocarlos, con dos botones: **House rule it** (solo para tarjetas: abilities, spells, skills, kin, professions, weapons, armor y gear) abre el formulario para cambiarla y, al guardar, queda en **tu** pack con `replaces: <clave>` — en el Reference **reemplaza** a la original para todos (el original no se toca) y la tarjeta se marca *House rule · <tu pack>*; **Copy to my pack** hace una entrada nueva e independiente (también para trampas, tablas, etc., que no se pueden reemplazar). Solo puede haber una house rule por tarjeta; en la lista del otro pack las que ya tienen una dicen *house-ruled*, y en tu lista las que reemplazan algo dicen *house rule* y muestran la clave que reemplazan. Lo que el formulario no tiene como campo (por ejemplo las listas de nombres de un kin) se conserva de la original.

Rutas (solo con el token del máster): `GET /api/gm/homebrew` (nombre del pack y qué se puede hacer), `PUT /api/gm/homebrew/pack` (`{"name"}`), `GET|POST /api/gm/homebrew/creatures` y `PUT|DELETE .../creatures/<id>`; la **imagen** de una criatura: en el formulario, **Upload picture** (PNG, JPEG o WebP; se achica a 900 px) la sube con `POST /api/gm/homebrew/creatures/image` (`{image: base64}`, devuelve `{path}`), queda en `images/creatures/` del pack, y el campo `image` de la criatura la nombra al guardar (vacío la quita; si el formulario no dice nada, la deja como estaba). La criatura la muestra en el Reference con su ficha; para el resto `GET|POST /api/gm/homebrew/<tipo>` y `PUT|DELETE /api/gm/homebrew/<tipo>/<posición>?name=<nombre actual>` (el nombre evita pisar otra entrada si la lista cambió). Para mirar otros packs: `GET /api/gm/homebrew/packs` y `GET /api/gm/homebrew/<tipo>?pack=<id>` (con la clave de cada tarjeta, `houseRuled` y `canReplace`); una house rule es un `POST /api/gm/homebrew/<tipo>` con `houseRuleOf: {pack, name}` (400 si no es una tarjeta, o si ya hay una para ella). Las tarjetas del Reference traen `editedBy` (el pack cuya house rule las reemplazó). Se valida todo y un error llega con su motivo (400, o 404 si la entrada ya no está).

## Adventures (máster)

La pestaña **Adventures**, solo del máster, lee las aventuras que tengas cargadas: un **desplegable** con una por cada pack que tenga un `adventure.yaml` (las del libro, en `data/packs/<id>/`, y las del usuario, en la carpeta de packs; si el id se repite gana la del usuario). Un pack del libro como `data/packs/mistyvale` **se carga como cualquier pack**, junto a Core: sus criaturas, tablas y demás aparecen en el Reference con su fuente, y se puede apagar en Settings. Los NPCs con nombre de Misty Vale están en su `creatures.yaml`; los grupos y los monstruos siguen en Core. Un pack del libro como `data/packs/mistyvale` **se carga como cualquier pack** (junto a Core: sus criaturas, tablas y demás aparecen en el Reference con su fuente, y se puede apagar en Settings); los NPCs con nombre de Misty Vale están en su `creatures.yaml`, y los grupos y monstruos siguen en Core. Se abre la primera. A la izquierda el **índice** (capítulos, secciones y lugares numerados, que se abren y se cierran) y a la derecha el texto de lo elegido, con sus viñetas ✦ en lista. Un **capítulo** muestra su texto y la lista de lo que tiene; un lugar o sección muestra su texto y, debajo, todo lo que contiene en el orden del libro: NPCs y monstruos (con el nombre de su criatura de Core), sidebars, tablas (con su botón *Roll*) y los textos «Continued». Arriba, la ruta (*Outskirt › Locations*) para volver. En el teléfono el índice y el texto se turnan.

Debajo del desplegable hay dos pestañas: **Story** (lo de arriba) y **NPCs**, que junta en un solo lugar todos los NPCs de la aventura: la lista agrupada por capítulo (con el lugar donde está cada uno), un buscador, y la casilla *Monsters too* para sumar los monstruos. Al elegir uno se ve su texto, dónde está (*Outskirt › Locations › 4. The Three Stags*), su **ficha de Core** (estadísticas, ataques y habilidades) y un enlace *Open in the story* que te lleva al lugar. Debajo de todo hay una caja de **Notes** para tus notas sobre ese NPC: se guardan solas un instante después de la última tecla (y al salir de la caja o abrir otro NPC); los NPCs con notas llevan un ✎ en la lista. Las notas son tuyas, no del pack: viven en `<carpeta del máster>/adventure-notes/<aventura>.yaml` (`GET /api/gm/adventures/<id>` las trae en `notes`, y `PUT /api/gm/adventures/<id>/notes` con `{node, text}` las guarda; un texto vacío la borra). En el teléfono la lista y el NPC se turnan.

**Palabras que son enlaces.** En el texto, las palabras importantes de la aventura (*Um-Durman*, *Azrahel Koth*, *Eledain*, *Outskirt*, *Fort Malus*, los nombres de las personas y de los lugares que el libro repite…) son enlaces con puntitos: un click lleva a su página (el índice se abre hasta ella), y a una persona la lleva a su página en la pestaña NPCs. Cada página se enlaza una sola vez por texto y nunca a sí misma, y arriba aparece **↩ Back** para volver a donde estabas. Las palabras salen del campo `keywords:` de cada nodo del `adventure.yaml`: lo escribe `tools/mistyvale.py` (títulos de capítulos y lugares, nombres de personas, y los nombres propios que el libro repite al menos 5 veces y tienen página), y a dónde va una palabra que no dice su página (*Um-Durman* va a «The Crypt of Um-Durman», *Azrahel Koth* a su ficha, *Eledain* a «Eledain’s Empire») se elige en `KEYWORD_TARGETS` del script.

**Imágenes.** Las ilustraciones del PDF salen en su lugar: la tira de mapa que abre cada capítulo, el **plano** de cada lugar (un click lo agranda a pantalla completa), el **retrato** de cada NPC o monstruo junto a su nombre y en su página de la pestaña NPCs, y las demás ilustraciones junto al texto al que acompañan. Las sirve `GET /api/gm/adventures/<id>/image?path=images/…` (solo con el token del máster, y solo archivos de imagen dentro de la carpeta `images/` del pack).

Es solo lectura y por ahora no está ligado al resto (los NPCs aparecen como etiqueta, sin abrir la criatura). El archivo lo hace `tools/mistyvale.py` a partir del PDF (ver `tools/README.md`): `adventure.yaml` es un árbol de `chapters` con `sections` anidadas (`id`, `title`, `kind`, `page`, `body`, y según el caso `number`, `creatures: [clave de Core]` o `table: <nombre en tables.yaml>`) y `tables.yaml` trae las tablas. Rutas (solo con el token del máster): `GET /api/gm/adventures` (la lista) y `GET /api/gm/adventures/<id>` (el árbol, más `tables` —solo las que nombran sus nodos— y `creatures` —clave → nombre—).

## Master: el tablero (máster)

La pestaña **Master** es un **lienzo a pantalla completa**, como un Miro liviano, donde ponés a mano lo que querés tener a la vista durante la partida:

* **📌 Pin desde cualquier lado.** Un botón **📌 Pin** (solo el máster lo ve) en una entrada de **Rules** (reglas, hechizos, equipo…), en una **criatura**, en una página de una **aventura** o en un **NPC** de ella, y en cada **tabla** (el 📌 chiquito junto a su título). Clavar algo lo copia al tablero tal como se ve en ese momento (texto, campos, tablas, ficha de la criatura) y aparece un aviso abajo. Clavar lo mismo dos veces no lo duplica.
* **Notas.** **+ Note** (con tres colores más) o **doble click** en el fondo; se escribe directo y se guarda solo.
* **Flechas.** Pasá el mouse por una tarjeta y arrastrá el **➜** del borde hasta otra: queda una flecha. Un click en la flecha la selecciona: le podés poner un **nombre** o borrarla.
* **Del ancho del resto de la app**, y tan alto como la ventana deja (hasta el borde de abajo; la página de atrás no se desplaza). El botón **⤢ Full screen** lo pone encima de todo, pestañas incluidas, y **Esc** (o ⤡ Exit full screen) vuelve.
* **Moverse.** Arrastrar el fondo mueve el lienzo; la **rueda** (o dos dedos) acerca y aleja; **−, %, +** y **Fit** (muestra todo) arriba a la izquierda. Una tarjeta se arrastra por su barra de arriba, se agranda por la esquina de abajo y se quita con la ×; **Supr** borra lo seleccionado.

El tablero (sus tarjetas, flechas y la posición del lienzo) se guarda **solo en el servidor del máster** (`<carpeta del máster>/board.json`: `GET` y `PUT /api/gm/board`, hasta 4 MB, 400 tarjetas) un instante después de cada cambio, así que lo encontrás igual al volver o desde otro equipo; si no se pudo leer nunca se pisa. También se puede abrir una pestaña del máster por su dirección (`…/?gm=…#master`), y al recargar vuelve a la misma.

## Master: Campaigns y Sessions

La pestaña **Master** tiene dos partes (los botones de arriba): **Master Screen**, el tablero de arriba, y **Campaigns**. Una **campaña** es lo que se juega: tiene un nombre, los **personajes que la juegan** y sus **sesiones**; una sesión es un tablero del mismo tipo que la Screen, **uno por cada partida jugada**: lo que pasó, lo que encontraron.

**Todo en una sola línea:** campaña, personajes, sesión, etiqueta de estado y botones (*+ Session*, *+ Campaign*, *End/Open Session*) van en una barra; en pantallas angostas pasa a la línea de abajo. Renombrar es el **✎** (el desplegable se vuelve un campo: Enter guarda, Esc deja el nombre como estaba) y borrar es el **✕**; las confirmaciones aparecen en una línea debajo de la barra.

* **Campaña.** Se elige con el desplegable **Campaign**, se renombra escribiendo sobre su nombre, y **+ New Campaign** crea una (*Campaign 2*). **Delete campaign** (pide confirmación, dice cuántas sesiones se van) la borra **con todas sus sesiones**. Hace falta una campaña para poder crear sesiones.
* **Personajes.** El desplegable **Characters** abre una lista con una casilla por cada personaje: marcás los que juegan esa campaña (un personaje puede estar en varias) y el botón muestra sus nombres. Se guarda al marcar.
* **Sesiones.** El desplegable **Session** lista las de **esa campaña**.

* **Una sola sesión activa.** **+ New Session** crea una en la campaña elegida, con nombre automático (*Session 3*) y la fecha de inicio, y pasa a ser **la activa**; **solo hay una sesión activa en toda la app** (aunque sea de otra campaña): si ya había una, avisa que la **cierra** antes. El nombre se cambia escribiendo sobre él; el desplegable lista todas con su día (*Oct 2, 2026*), la más nueva primero y la activa con ●; al lado, una etiqueta *Active* o *Closed*.
* **End Session** (pide confirmación) la **cierra con su fecha**: queda como está, se puede mirar y moverse por ella, pero **no se puede cambiar** (sin botones de agregar, tarjetas fijas, notas de solo lectura). Después de cerrar **no queda ninguna activa** hasta que crees otra o abras una.
* **Delete** (pide confirmación) borra la sesión **para siempre**, con todo lo que tiene (si era la activa, no queda ninguna). Las imágenes que trajiste siguen en el servidor.
* **Open Session** (en una cerrada) la **reabre para editarla** y pasa a ser la activa (la que estaba activa se cierra). Se anota cuántas veces se reabrió.
* **Enviar desde la Master Screen.** Cada tarjeta de la Screen trae un **→** en su barra: manda una **copia** a la sesión activa (el original se queda en la Screen) y avisa «Sent to “Session 3”». Una nota con *“Pelearon contra el ogro y ganaron; botín: un anillo de plata”*, la ficha de la criatura, una tabla o una imagen: todo se puede enviar. El cartelito de arriba de la Screen dice la campaña y la sesión activa (*→ The Misty Vale · Session 3*), o *No active session*, y entonces el → está apagado.

**Imágenes.** Se pueden poner en cualquier tablero: **+ Image** (elegir un archivo), **pegar** (Ctrl+V) o **arrastrar** un archivo sobre el lienzo, y las del libro con el **📌** que aparece en la esquina de cada mapa o retrato de **Adventures**. Las tuyas se guardan en el servidor (`<carpeta del máster>/board-images/`, se achican a 1600 px, hasta 8 MB); las del libro se enlazan, no se copian.

Cada campaña es un archivo del servidor (`<carpeta del máster>/campaigns/<id>.json`) y cada sesión otro (`<carpeta del máster>/sessions/<id>.json`, hasta 4 MB y 400 tarjetas; las sesiones de antes de que existieran las campañas quedan en una llamada *Campaign 1*). Rutas (solo con el token del máster): `GET|POST /api/gm/campaigns`, `PUT|DELETE /api/gm/campaigns/<id>` (nombre y personajes; `DELETE` borra también sus sesiones), `GET /api/gm/sessions` (lista, con la campaña de cada una, y cuál es la activa), `POST /api/gm/sessions` (crear, con `campaign`), `GET|PUT|DELETE /api/gm/sessions/<id>` (la sesión, su nombre y su tablero; un tablero cerrado responde 409; `DELETE` la borra), `POST /api/gm/sessions/<id>/end` y `/open`, `POST /api/gm/board/images` y `GET /api/gm/board/images/<id>`. La parte abierta va en la dirección (`…#master/campaigns`).

## Backup: llevar el trabajo a otra computadora

El botón **Backup** del encabezado del máster (a la derecha) abre dos opciones:

* **Export backup** descarga un solo archivo, `skaldbok-backup-AAAA-MM-DD.json`, con **todo el trabajo del máster**: personajes, chat, campañas, sesiones, tablero y sus imágenes, notas de aventuras y homebrew. Las imágenes van dentro (en base64), así que el archivo pesa más que la carpeta.
* **Import backup…** (en la otra computadora): eliges el archivo, te dice cuántos archivos trae y pide confirmación; **reemplaza todo** lo que había (lo de ahí se borra) y recarga la página.

Lo que **no** viaja, porque es de cada computadora: los ajustes (`settings.*`), el estado del servidor y los **enlaces de los jugadores** (`web-access.*`): en la otra máquina los personajes reciben enlaces nuevos. Tampoco viajan los packs de contenido ni los datos del libro: cada máquina trae los suyos. Si el archivo no es un backup, o trae nombres de archivo no permitidos (`..`, rutas absolutas), se rechaza entero y no se toca nada.

Rutas (solo máster): `GET /api/gm/backup` y `POST /api/gm/backup/import` (hasta 300 MB). Código: `src/web/web_backup.*`, `BackupMenu.vue`.

## NPC Creator (solo máster)

En **Reference › World**, debajo de *Non-Player Characters*, hay una entrada **NPC Creator**: el botón **Create Random NPC** saca un valor al azar de cada lista de *Creating NPCs* (nombre, attitude, kin, motivation, profession y trait) y cada resultado tiene su propio botón **Re-roll**. Las listas salen de `data/system/npcs.yaml` (`GET /api/npcs`, o `GET /api/gm/npcs`); la entrada es un nodo de `rules.yaml` con `tool: npc-creator`.

## Lo normal: abrir la app y listo

**`Skaldbok.bat`** abre la app del máster. **El servidor web está apagado hasta que lo arrancás**: con **Start server** en **el engranaje › Web server**
(el engranaje de arriba a la derecha; también sirve el botón *Web server: off* de la barra de arriba, que muestra si está funcionando), o marcando
**Launch server when opening** para que arranque cada vez con la app. La app lo cierra al salir; incluso si la app se cierra a la fuerza, el servidor se apaga solo. En **el engranaje › Web server** ves si está funcionando,
la dirección para los jugadores (la de tu red, por ejemplo `http://192.168.1.20:8080`) y el enlace personal de cada personaje con
*Copy* y *Send in a message*. El enlace de un jugador es esa dirección más su token: `http://192.168.1.20:8080/?t=<token>`.

* Los jugadores tienen que estar en la **misma red Wi-Fi** (o usar un túnel, ver más abajo).
* La primera vez, Windows pregunta si permitir `skaldbok_web` en la red: **permitir en redes privadas**.
* Si el puerto 8080 lo usa otro programa (Jellyfin usa 8096, otros usan 8080), cámbialo en el engranaje › Web server › *Port* › *Apply*.
* En el engranaje › Web server se puede activar el arranque con la app (*Launch server when opening*).
* La página necesita compilarse una vez (`cd web && npm install && npm run build`); `Skaldbok.bat` lo hace solo si falta y hay Node.

## Ponerlo en marcha a mano (sin la app, por ejemplo en otro equipo)

```
# 1) compilar la página (una vez, con Node 20+)
cd web && npm install && npm run build

# 2) compilar el servidor junto con el resto del proyecto
powershell -ExecutionPolicy Bypass -File scripts\build.ps1        # crea build\release\skaldbok_web.exe

# 3) arrancarlo
build\release\skaldbok_web.exe --public-url http://mi-direccion
```

Al arrancar crea el archivo `web-access.yaml` (en la carpeta del usuario) con un **token secreto por personaje**. Los enlaces:

```
skaldbok_web --links                    # imprime el enlace de cada jugador
skaldbok_web --regenerate Brenna        # enlace nuevo para ese personaje: el viejo deja de funcionar al instante
```

También aparecen en la app del máster, en la ficha de cada personaje (**Player's web page**), con botón *Copy link* y *Send it
in a message*. Un personaje nuevo recibe su enlace en cuanto el servidor lo ve.

### Opciones

Variables de entorno o argumentos (`--port`, `--host`, `--public-url`, `--prefs`, `--data`, `--static`):

| | por defecto |
|---|---|
| `PORT` | `8080` |
| `HOST` | `0.0.0.0` (todas las interfaces; usa `127.0.0.1` para solo esta máquina) |
| `PUBLIC_URL` | `http://localhost:<puerto>`: la dirección que escriben los jugadores; va dentro de los enlaces |
| `SKALDBOK_PREFS` | la carpeta del usuario de la app (`%APPDATA%\skaldbok\gm`) |
| `SKALDBOK_DATA` | la carpeta `data/` con `packs/core` (se busca junto al ejecutable) |
| `STATIC_DIR` | la página compilada (`web/` junto al ejecutable, o `web/client/dist`) |
| `MAX_FAILURES`, `FAILURE_WINDOW_MS` | 20 intentos fallidos por dirección cada 10 minutos, luego se bloquea |
| `TRUST_PROXY` | `1` si está detrás de un túnel/proxy: toma la dirección real de `X-Forwarded-For` |

## Que los jugadores lleguen desde fuera de tu casa

En la misma Wi-Fi no hace falta nada: la pestaña Web server ya da enlaces con la dirección de tu PC. Desde otra red hay que exponerlo, y
conviene **HTTPS** (el enlace lleva el token; sobre HTTP plano viajaría a la vista). El servidor habla HTTP simple a propósito;
hay tres caminos, del más fácil al más trabajoso:

**A. Cloudflare Tunnel rápido (sin cuenta, sin abrir puertos, HTTPS incluido).** Con la app abierta:

```
winget install Cloudflare.cloudflared          # una vez
cloudflared tunnel --url http://localhost:8080
```

Escribe una dirección `https://algo-al-azar.trycloudflare.com`. Cópiala en **el engranaje › Web server › Address players use › Apply**: los
enlaces de los jugadores pasan a usar esa dirección (y el servidor cuenta los intentos fallidos por cliente real, no por el túnel).
Los jugadores los abren desde cualquier lugar, sin instalar nada. Limitaciones: la dirección **cambia cada vez** que reinicias
`cloudflared` (hay que volver a copiar los enlaces) y el túnel solo existe mientras esa ventana esté abierta. Para una dirección
fija hace falta una cuenta gratuita de Cloudflare y un dominio propio (túnel con nombre).

**B. Tailscale (red privada, sin HTTPS que configurar, sin exponer nada a Internet).** Tú y cada jugador instalan Tailscale
(gratis) y tú los invitas a tu red (o compartes tu PC con ellos). Después cada jugador abre
`http://<nombre-o-ip-100.x.x.x-de-tu-pc>:8080/?t=<token>`: el tráfico va cifrado por Tailscale. Pon esa dirección en
**Address players use**. Es lo más privado, pero cada jugador tiene que instalar Tailscale.

**C. Abrir el puerto en el router** (reenvío del 8080 + una dirección fija o DDNS) y un proxy inverso (Caddy, nginx) con
certificado. Funciona, pero expone tu casa a Internet y hay que mantenerlo: solo si sabes lo que haces.

Una dirección que empieza por `https://` en **Address players use** hace que la app arranque el servidor con `--trust-proxy`
(usa `X-Forwarded-For` como dirección del cliente). Si lo arrancas a mano detrás de un proxy, agrégalo tú
(`skaldbok_web --trust-proxy`, o `TRUST_PROXY=1`). No lo actives si el servidor está expuesto directamente, sin proxy.

## La hoja del jugador

La pestaña **My character** sigue la primera hoja (la verde) de la hoja de personaje oficial y está pensada para el teléfono: nombre en el
pergamino, las seis gemas de atributos con su condición (el rombo se enciende en rojo), PV y PW como círculos que se vacían, bonos y
movimiento, habilidades en pergamino (rombo = marca de avance; las no entrenadas, en gris con su probabilidad base), habilidades y
hechizos que se abren con su texto, armas, armadura y casco con su valor y su *Bane on*, inventario numerado, monedas, recuerdo y objetos
diminutos. En pantallas anchas usa las tres columnas de la hoja impresa; en el teléfono, un solo carril en el orden en que se usa en la mesa.

## Chat con el máster

`GET /api/chat` devuelve la conversación del jugador (más vieja primero: `id`, `at`, `from` = `gm` o `player`, `kind` = `message` o `broadcast`, `to`, `text`,
`image`) y dónde leyó cada lado (`gmRead`, `playerRead`). `POST /api/chat` `{text?, image?}` escribe al máster (texto de hasta 4000 letras; `image` en base64:
PNG, JPEG, GIF o WebP de hasta 8 MB, reconocido **por sus bytes**, no por un nombre: un SVG, que puede llevar código, se rechaza). `POST /api/chat/read`
`{upTo}` marca lo leído. `GET /api/chat/<id>/image` sirve una imagen **solo de la conversación del propio jugador**: el archivo sale de la conversación, nunca de la
petición, y otro jugador (o una ruta con `..`) recibe 404. La página baja la imagen con el token en la cabecera y la muestra desde una dirección `blob:` (la CSP
permite `img-src blob:`). Un jugador nunca escribe a otro jugador: su única conversación es con el máster.

## Editar la hoja

`GET /api/me` trae, además de lo derivado, el `doc` (los campos editables tal cual están en el archivo), `issues` (lo que rompe las reglas y nadie aprobó, cada uno con
`key`, `message` y `status` = `pending` o `rejected`), `locked` y `revision`. `PATCH /api/me` `{set: {...}}` cambia campos: los valores van enteros, salvo
`attributes` y `coins`, que se mezclan clave por clave. Se valida el tipo y el tamaño de todo (400 con el motivo si algo está mal, sin aplicar nada) y solo se aceptan los campos
de la hoja: `kin`, `profession`, `school`, `id`, `revision`, `locked` y `reviews` son del máster (400). Una hoja bloqueada devuelve 423. **Romper una regla no se rechaza**: se guarda y
queda marcado en `issues`. La respuesta es la hoja nueva. Cada cambio queda en `changes/<personaje>.json` (quién, qué y el valor anterior) y sube el `revision`.

Como la app del master y el servidor escriben los mismos archivos, ninguno reescribe la hoja entera: cada escritura toma el archivo tal como está y le aplica
solo lo que cambió, de forma atómica; la app compara el **contenido** de los archivos (no sus fechas, que solo tienen la precisión del reloj del sistema, ~15 ms).

## Qué ve un jugador, y qué no

Cada respuesta se arma campo por campo en `src/web/web_views.cpp`; nada se pasa "tal cual", así que un campo nuevo en los
archivos del máster **sigue siendo privado** hasta que se añade a propósito.

De sí mismo: su ficha completa — atributos, PV/PW, condiciones, habilidades, aptitudes y hechizos con su texto, equipo con sus estadísticas, monedas, debilidad, recuerdo, apariencia y las **notas de la ficha**.

Además, las **reglas**: una sola pestaña **Rules** con el mismo índice que usa el máster en su pestaña Rules (tipos de contenido con su cantidad y capítulos de reglas, en orden alfabético). Un capítulo con el mismo título que un tipo (p. ej. Skills) se fusiona en una entrada con dos pestañas: "All skills" y "General info". **Magic** reúne Spells y el texto general (pestañas Spells y General Info). **Gear** agrupa cuatro pestañas: General, Weapons, Armor y General Info (el texto general de Gear, con Supply y Encumbrance) (ya no hay entradas sueltas de Weapons y Armor). Todo del Core y los packs de homebrew **activos**. El código es compartido con la vista del máster (`reference.js`, `ReferenceIndex.vue`, `ReferencePanel.vue`).

**Nunca**: criaturas y tablas (son del máster), las conversaciones de otros jugadores, las fichas de
otros jugadores, ni los tokens. Un pack que el máster apaga o quita deja de verse en la web.

Seguridad:

* El token son 8 caracteres hex (32 bits aleatorios; lo que frena las adivinanzas es el límite de intentos fallidos por dirección; al arrancar, el servidor reemplaza los tokens viejos más largos por otros de 8, así que esos links dejan de valer); identifica **un** personaje y el personaje lo elige el servidor por el token, nunca un parámetro.
* El token va en la cabecera `Authorization: Bearer` (nunca en la URL de la API, para que no quede en registros). La página lo
  guarda en el navegador y lo **deja en la barra de direcciones** (`?t=…` o `?gm=…`): cada pestaña dice quién es, así que se puede abrir la del máster y la de un jugador a la vez en el mismo navegador, y cada una se recarga como lo que era (una pestaña con `?t=` es de jugador aunque el enlace del máster esté guardado).
* Tras 20 intentos fallidos desde una dirección se bloquea unos minutos. Los mensajes de error no dicen por qué falló.
* GET, y solo dos escrituras (`PATCH /api/me`, `POST /api/chat` y su marca de leído); el token va en una cabecera, no en una cookie, así que no hay CSRF; hasta 60 escrituras cada 10 s por personaje; cuerpos de más de 64 KB se rechazan (salvo una imagen del chat, hasta 12 MB en base64); respuestas de la API con `Cache-Control: no-store`; cabeceras `nosniff`, `X-Frame-Options: DENY`,
  `Referrer-Policy: no-referrer` y una CSP estricta; los textos se muestran siempre como texto (nunca como HTML).
* Los archivos del máster no se escriben de forma atómica: si una petición cae justo en medio de un guardado, el servidor sigue
  mostrando la última versión buena, y **nunca** borra el token de un jugador por no poder leer su archivo un instante.

## API (para quien quiera hacer otro cliente)

Todas las rutas menos `/api/health` piden `Authorization: Bearer <token>`.

| | |
|---|---|
| `GET /api/health` | `{"ok":true}` |
| `GET /api/me` | la ficha del jugador, con los números derivados (movimiento, bonificación de daño, carga, nivel de cada habilidad) |
| `GET /api/content` | tipos de contenido (con su cantidad), packs cargados y capítulos de reglas (`rules[]`; los marcados `introOnly: true` muestran la intro del tipo en lugar de un capítulo YAML) |
| `GET /api/content/<tipo>?q=texto` | `spells`, `abilities`, `skills`, `kin`, `professions`, `weapons`, `armor`, `gear`; cada entrada tiene `key`, `kind`, `name`, `subtitle`, `fields`, `body`, `tables`, `homebrew`; `source` solo para homebrew. `tables` (y `intro.tables`, y `tables` de cada regla) son `{title, dice, columns[], rows[{roll, cells[]}]}`: las propias más las que el texto nombra con una línea `{{table: Nombre}}`, que el cliente dibuja en ese lugar |
| `GET /api/creatures?q=texto`, `GET /api/creatures/<clave>`, `GET /api/creatures/<clave>/image`, `GET /api/npcs` | las criaturas del Bestiary (lista con etiquetas NPC / Animal / Monster, ficha completa e ilustración) y las listas del NPC Creator. `contentSummary` trae también el tipo `creatures` |
| `GET /api/rules/<cap>` | capítulo de reglas YAML por clave (`combat`, `world`, …); devuelve `{key, title, rules[]}` con cada nodo `{key, title, body, sections, parentId}` |

La **Reference es idéntica para el máster y para los jugadores**: son las mismas rutas de solo lectura, con el token del jugador bajo `/api/…` y con el del máster bajo `/api/gm/…` (mismas respuestas). Lo que sigue siendo solo del máster es lo demás de `/api/gm/*`: personajes ajenos, chat, creador de personajes.

Errores: `401` enlace no válido, `429` demasiados intentos, `404` ruta o tipo desconocido, `405` método no permitido.

## Desarrollo de la página

```
cd web
pm run dev          # http://localhost:5173, reenvía /api a http://localhost:8080 (API_URL para otro)
pm test             # Vitest: enlace y token, la ficha, el refresco, la desconexión, las tabs
pm run build        # web/client/dist
```

Con `skaldbok_web` corriendo en el puerto 8080, `npm run dev` recarga la página al vuelo. Las pruebas del cliente usan
respuestas de ejemplo (`web/client/src/test/fixtures`) hechas con datos inventados, no con texto de los libros.

## Pruebas del servidor

`tests/web_test.cpp` (parte de `ctest`): autenticación, la ficha y sus números, qué se filtra y qué no, las reglas y
los packs apagados, cambios en vivo, archivos a medio escribir, tokens (persistencia, regeneración, personajes nuevos y
borrados), bloqueo por intentos, la página estática y HTTP real (sockets). Usa archivos escritos por la propia app del máster
(`tests/fixtures/web`) y un Core inventado, así que corre sin los libros; con `data/` presente además comprueba las claves
reales del Core.
