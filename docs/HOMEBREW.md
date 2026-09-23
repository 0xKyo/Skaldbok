# Packs de contenido (homebrew)

Todo el contenido del juego —reglas, criaturas, hechizos, aptitudes, habilidades, razas, profesiones, equipo y tablas— vive en
**packs**. El contenido de los libros es un solo pack incorporado, **Core** (`data/packs/core`): en su `rules.json` van las reglas
genéricas, que no necesitan una categoría en particular, con sus tablas y la aventura; y en los demás archivos, las criaturas,
hechizos, razas... Cuando una categoría crece demasiado puede pasar a su propio JSON. Core se abre primero y el homebrew se suma a él:
nunca lo reemplaza (salvo una regla con `replaces`, ver `rules.json`). Cada entrada mantiene su **fuente**: lo de los libros aparece
como *Rulebook / Bestiary / Adventure*; lo de un pack de homebrew como *Homebrew · <nombre>*. El id `core` está reservado.

Un pack es una **carpeta** (o un **.zip** de esa carpeta) con:

```
mi-pack/
  manifest.json        opcional: sin él, el id es el nombre de la carpeta (para importar el pack, hace falta)
  rules.json           un archivo JSON por tipo; todos opcionales (las homerules van aquí)
  creatures.json
  spells.json
  abilities.json
  skills.json
  kin.json
  professions.json
  weapons.json
  armor.json
  gear.json
  tables.json
  images/              arte de las criaturas (.png .jpg .gif .bmp)
```

Para importarlo: **Settings › Content packs › Import a pack folder… / Import a .zip…**. Se copia a la carpeta de packs del
usuario (`Settings › Where things are`), se puede apagar sin borrarlo y se puede quitar con *Remove*. Importar otra vez un pack
con el mismo `id` lo **actualiza**. Un ejemplo completo está en [`examples/frostmarch-tales/`](../examples/frostmarch-tales).

Para comprobar un pack antes de repartirlo, desde una terminal:

```
pack_check mi-pack            # o mi-pack.zip
pack_check mi-pack --strict   # los avisos también cuentan como fallo
```

`pack_check` usa el mismo código que la app: si dice OK, la app lo importa. Nada se instala.

Un pack ya instalado se puede editar con la app abierta: al guardar un `.json` o cambiar una imagen en la carpeta del pack
(`Settings › Where things are`), la app lo relee sola en un segundo. Si el archivo quedó mal escrito, avisa cuál y el pack se
omite hasta que lo arregles; al guardar de nuevo se vuelve a leer.

## Reglas generales de los archivos

* JSON en UTF-8. Se aceptan comentarios `//` y `/* */` y el BOM que agregan algunos editores de Windows.
* Cada archivo es `{ "spells": [ … ] }` (la clave es el nombre del archivo) o directamente una lista `[ … ]`.
* Los textos largos pueden escribirse como **lista de párrafos**: `"description": ["Primer párrafo.", "Segundo."]`.
* Los números pueden ir como texto (`"rank": "1"`, `"movement": "9"`). Los campos desconocidos se ignoran, así que
  un pack escrito para una versión futura sigue cargando.
* Toda entrada necesita `name`. Sin nombre se **omite** con un aviso; el resto del pack carga igual.
* `id` es opcional (se deriva del nombre). Debe ser único dentro de su archivo; si se repite, se renumera y se avisa.
* `source` y `page`: ver más abajo. Una entrada dice su página una sola vez, con `page`.
* `fields`: cualquier ficha (menos criaturas y tablas) acepta `"fields": { "Etiqueta": "valor" }` para mostrar líneas
  extra, sin tocar el código.
* `tables`: cualquier ficha (hechizo, raza, profesión, equipo...) puede llevar **sus propias tablas**, con el mismo formato que `tables.json`
  (`name`, `dice`, `columns`, `rows`, `page`); heredan la `source` de la ficha. Se ven al abrir la tarjeta («Click to expand») y en el Master Screen
  cuando se fija; la búsqueda las cubre. No aparecen entre las tablas de las reglas (Rules).
* Un pack con JSON inválido, un `manifest.json` roto o hecho para un formato más nuevo **se rechaza entero**, con el
  archivo y la posición del error. Los problemas de una sola entrada nunca rechazan el pack.

## Descripción del pack: manifest.json, o la cabecera de un JSON

Un pack se describe (nombre, autor, fuentes...) en un `manifest.json`, **o, si no tiene, en la cabecera de su primer archivo de datos
que la tenga**: las mismas claves, arriba de todo en el JSON del tipo (se prueba en el orden `rules`, `spells`, `abilities`, `skills`,
`kin`, `professions`, `weapons`, `armor`, `gear`, `tables`, `creatures`). Si hay las dos cosas, manda el `manifest.json`. El `id` de un
pack sin manifest es el nombre de su carpeta (minúsculas, dígitos, `-` y `_`); una carpeta sin manifest ni archivos de datos no es un
pack. Importar un `.zip` o una carpeta pide `manifest.json`, porque el `id` no se puede deducir de una carpeta temporal.
```json
{ "name": "Mi Tome", "author": "Alguien", "sources": [ { "key": "tome", "title": "Mi Tome", "short": "Tome" } ],
  "spells": [ { "name": "Zap" } ] }
```
(Así se describe Core: en la cabecera de su `rules.json`.) Con un `manifest.json`:

```json
{
  "format": 1,
  "id": "frostmarch",
  "name": "Frostmarch Tales",
  "version": "1.0.0",
  "author": "Alguien",
  "description": "Una frase sobre el pack.",
  "sources": [
    { "key": "frostmarch", "title": "Frostmarch Tales", "short": "Frostmarch" }
  ]
}
```

| campo | |
|---|---|
| `format` | siempre `1` por ahora. Un número mayor lo rechaza esta versión de la app |
| `id` | Minúsculas, dígitos, `-` y `_` (`mi-pack`). Identifica al pack; `core` está reservado. Sin manifest.json (o sin `id`), es el nombre de la carpeta |
| `name` | nombre a mostrar (por defecto, el `id`) |
| `version`, `author`, `description` | informativos |
| `sources` | opcional. Los libros o suplementos de donde sale el contenido. Sin esto, hay una fuente con el nombre del pack |

Cada fuente se muestra como **Homebrew · `short`** y tiene su propio botón/filtro (menú *Homebrew* de la barra superior),
su color y su etiqueta en cada tarjeta. Una entrada elige su fuente con `"source": "<key>"`; sin él usa la primera.

### Páginas

`"page": 12` es solo una referencia para mostrar (*p.12*): un pack no tiene PDF, así que no hay botón «Original». Las entradas de
los libros (Core) sí llevan enlace a la página del PDF, y ahí `page` es la **página física del PDF**: es la única que hace falta
escribir. El número impreso en la página se deduce de la cabecera del libro (en Core, la de `rules.json`):

```json
{ "key": "adventure", "title": "…", "short": "Adventure", "file": "References/MistyValeAdventure.pdf", "pages": 120,
  "page_offset": 2, "first_numbered_page": 5, "unnumbered_pages": [6, 7, 8] }
```
El número impreso es la página del PDF menos `page_offset`, desde `first_numbered_page` (por defecto, la que sigue al offset), salvo las
`unnumbered_pages`, que no tienen número. Si un libro no sigue ninguna fórmula, `"printed_pages": [0, 0, 5, 6, …]` da el número de cada
página. Ninguna entrada escribe el número impreso: solo `page`, y el número se deduce del libro.

## Tipos

Todos los campos son opcionales salvo `name`. Cualquier tarjeta (de cualquiera de estos archivos) puede llevar:
* `"image"`: una ilustración chica de la tarjeta, ruta relativa al pack (`"images/orco.png"`), igual que ya usan las criaturas.
* `"replaces"`: en vez de agregar una tarjeta nueva, **cambia** la que tenga esa clave (`"core/kin/human"`, por ejemplo): conserva su
  lugar y su clave, toma el resto de los datos de la nueva, y queda marcada *"Changed by \<pack\>"* — igual que `replaces` en una regla
  (ver más abajo). Así funcionan los botones **Edit** de Kin y Abilities en la app: sea una tarjeta del libro o una tuya, "editarla" en
  realidad escribe una tarjeta con `replaces` en tu propio pack de homebrew (`custom`, uno solo para todo lo que crees así); `Generate
  Kin`/`Generate Ability` crean una sin `replaces`, nueva. El original nunca se toca.

Cualquiera de estos archivos puede además llevar un `"intro"` de nivel superior
(junto a su lista, no dentro de una tarjeta): un texto general de esa categoría
(`{"intro": "...", "spells": [...]}`). También puede llevar sus propias secciones con título y sus propias tablas, igual
que una regla (`{"intro": {"body": "...", "sections": [{"name": "...", "body": "..."}], "tables": [...]}, "spells": [...]}`)
— así es como los capítulos "Skills", "Bestiary", "Magic" y "Gear" de Rules terminaron adentro de `skills.json`,
`creatures.json`, `spells.json` y `gear.json`. Un pack posterior con `intro` no vacío reemplaza el de uno anterior para esa categoría.

Si la categoría tiene mosaico (spells, abilities, skills, kin, professions) y además tiene `intro`, se muestra como una
pestaña "Intro" separada de la pestaña con las tarjetas (Spells, Abilities, Skills); sin `intro` no hay pestañas, solo el
mosaico, como siempre. Creatures (que no es un mosaico, es lista + detalle) hace lo mismo: "Intro" y "Creatures" como dos
pestañas. Weapons, armor y gear no tienen mosaico propio — su categoría **Gear**, en Reference, es solo esa página
(el `intro` de `gear.json`, con sus tablas).

### spells.json — hechizos y trucos
`school` (por defecto *General Magic*), `trick` (`true` = truco de magia), `rank`, `prerequisite`, `requirement`,
`casting_time`, `range`, `duration`, `description`.
Una escuela nueva (por ejemplo `"Frostcraft"`) aparece sola como filtro en el mosaico de Spells.

### abilities.json — aptitudes
`type` (`heroic` o `kin`), `kin` (para las innatas), `requirement`, `wp_cost`, `description`.

### skills.json — habilidades
`attribute` (`STR CON AGL INT WIL CHA`), `category` (`core`, `weapon`, `magic` u otra), `description`.
El atributo define la probabilidad base del personaje. Las de categoría `magic` no se eligen libremente al crear un
personaje (vienen con la profesión).

### kin.json — razas
`description`, `movement` (número), `innate_abilities` (nombres de aptitudes; pueden ser de cualquier pack cargado),
`names` (lista de nombres típicos: el creador de personajes los ofrece). Si el kin trae su **tabla de nombres** en `tables` (ver arriba), no hace falta la lista: los nombres son la primera columna de esa tabla (Core lo hace así: la tabla «Human: First Name» está en la ficha de Human).

### professions.json — profesiones
* `key_attribute`, `description`.
* `skills`: las habilidades entre las que se eligen las 6 de profesión.
* `heroic_abilities`: aptitudes heroicas entre las que se elige la inicial (de cualquier pack).
* `starting_gear`: los conjuntos de equipo inicial (típicamente 3), como texto. Dentro de un conjunto se separa con comas;
  `A/B/C` es una elección; `D6 food rations` / `D8 silver` se tiran solos. Si la profesión trae su propia tabla `tables` llamada
  `"<Profesión>: Gear"` (mismo formato que en cualquier ficha, ver arriba), no hace falta la lista: el creador toma cada conjunto de la
  primera celda de cada fila (Core lo hace así: la tabla de equipo de cada profesión vive en su propia ficha).
* `nicknames`: apodos típicos. Igual que `starting_gear`: si hay una tabla `"<Profesión>: Nickname"`, la lista sale de ahí.
* Magia: `"skills_by_school": { "Animism": [ … ], … }` y `"magic": { "spells": 3, "tricks": 3, "spell_rank": 1 }`.
  Si existe `skills_by_school` el creador pide elegir escuela, y la profesión no da aptitud heroica.

### weapons.json, armor.json, gear.json — equipo
* Arma: `kind` (`melee`/`ranged`), `grip`, `str_req`, `range`, `damage`, `durability`, `cost`, `supply`, `features`.
* Armadura: `slot` (`armor` o `helmet`), `armor_rating`, `cost`, `supply`, `effect`.
* Objeto: `category`, `cost`, `supply`, `weight`, `effect`.

Este equipo no tiene un mosaico propio: las armas, armaduras y objetos del libro son las tablas de la categoría **Gear**
(en Reference, con su propio `gear.json`, ver más arriba); se puede igual añadir a la ficha de un personaje, buscarlo con
`Ctrl+K` y fijarlo en el Master Screen.

### creatures.json — criaturas (un archivo grande)
```json
{
  "id": "frost-wight", "name": "Frost Wight", "kind": "monster", "category": "Undead",
  "quote": "…", "description": ["…", "…"],
  "image": "images/frost-wight.png",
  "attack_dice": "D6",
  "statblocks": [ { "variant": "", "fields": { "Ferocity": "2", "Size": "Normal", "Movement": "10", "Armor": "2", "HP": "18" } } ],
  "attacks": [ { "roll": "1-2", "name": "Rimed Claws", "text": "…" }, { "roll": "3-4", "name": "…", "text": "…" } ],
  "abilities": [ { "name": "Undead", "text": "…" } ],
  "random_encounter": "…", "adventure_seed": "…",
  "tables": ["Título de una tabla relacionada"]
}
```
* `kind`: `monster`, `npc` o `animal`. `category` agrupa la lista.
* `statblocks`: uno o varios (por ejemplo *Scout* y *Warrior*); `fields` conserva el orden que escribas. El campo **HP**
  se usa para «Add to encounter».
* `attacks`: la tabla de ataques de monstruo. `roll` es `"1"`, `"1-2"`…; sin `roll`, las filas se numeran en orden.
  Si `text` no empieza por el `name`, se antepone. `attack_dice` (por defecto `D6`) es el dado de esa tabla.
* `image`: ruta **relativa a la carpeta del pack**, dentro de ella. Una ruta con `..` o absoluta se ignora con un aviso.
* `stats_ref`: para criaturas cuyas estadísticas están en otra página («stats as per page 87»).

### tables.json — tablas
```json
{ "name": "Frostmarch Weather", "dice": "D6", "columns": ["WEATHER", "EFFECT"],
  "rows": [ { "roll": "1", "cells": ["Whiteout", "…"] }, { "roll": "3-4", "cells": ["Snowfall", "…"] } ] }
```
Con `dice` la tabla tiene botón *Roll* que resalta la fila. Sin `dice`, es una tabla simple (las filas pueden ser solo
listas de celdas). Dos campos opcionales: `"browse": false` la deja fuera de Rules, y
`"role": "weakness" | "memento" | "appearance"` hace que el creador de personajes tire en ella (además de las del Core).

**Dónde se ve una tabla.** No hay una lista aparte de tablas: se leen dentro de **Rules**, en la sección a la que pertenecen. Lo
más cómodo es escribirla **dentro de su regla**, con la clave `tables` (igual que `children`; la tabla toma la `source` de la
regla si no pone otra):
```json
{ "rules": [ { "name": "Foraging", "source": "zine", "body": "…",
    "tables": [ { "name": "Finds", "dice": "D6", "columns": ["FIND"], "rows": [ { "roll": "1-3", "cells": ["Berries"] }, { "roll": "4-6", "cells": ["Roots"] } ] } ],
    "children": [ … ] } ] }
```
Una tabla de `tables.json` (sin regla) aparece en Rules bajo una sección propia del pack, **«Tables · <nombre del pack>»**, al final
del árbol. Buscar una tabla (`Ctrl+K`), abrirla desde una criatura o desde el Master Screen lleva a su sección. Una criatura enlaza
su tabla con `"tables": ["First Name"]`; si existe una tabla llamada `<Criatura>: First Name` (por ejemplo `Goblin: First Name`),
enlaza a esa, y si no, a la primera con ese título.

### rules.json — reglas y homerules
```json
{ "rules": [
    { "name": "Camping", "source": "zine", "body": "Descansar bien cuesta una ración.",
      "children": [
        { "name": "Fogatas", "body": ["Mantené una encendida.", "Con lluvia, tirada de Supervivencia."] },
        { "name": "Guardias", "children": [ { "name": "Guardia de noche", "body": "…" } ] }
      ] },
    { "name": "Fumbles", "parent": "core/rule/combat-damage", "body": "Un 20 en un ataque es un desastre." },
    { "name": "Melee, a nuestra manera", "replaces": "core/rule/melee-combat", "body": "Sin paradas gratis." }
] }
```
Las reglas forman un **árbol** (lo que muestra el módulo Rules) y se escriben **anidadas**: una regla lista sus `children` dentro de sí
misma. Las de Core traen el texto de los libros; cualquier otro pack puede sumar las suyas:
* `name` (obligatorio) y `body` (un texto, o una lista de párrafos). `page` funciona como en los demás tipos.
* `source`: de dónde viene. Una regla **hija que no lo pone hereda el de su padre**, así que basta escribirlo en la raíz.
* `id`: opcional; sale del `name` en minúsculas (`Guardia de noche` → `guardia-de-noche`, y con un repetido `-2`). Solo hace falta
  ponerlo para fijar una clave que otros packs van a nombrar. La clave de cada regla es `<pack>/rule/<id>`
  (en Core: `melee-combat`, `boons-banes`; los títulos repetidos llevan delante el de su padre).
* `parent` (solo para reglas **en la raíz del archivo**): cuelga la regla de otra que ya existe. Es la clave completa de una
  regla de un pack anterior (`core/rule/combat-damage`) o el `id` de otra regla de este pack. Dentro de un árbol se ignora: manda
  el lugar donde está escrita. Si el padre no existe se avisa y va a la raíz.
* `replaces`: en lugar de agregar una regla, **cambia** la que tenga esa clave: conserva su lugar y su clave, toma el título y el
  texto nuevos, muestra la fuente del pack y dice *"Changed by <pack>"*. Sus `children` se agregan debajo. Si la regla no existe,
  se avisa y se omite.
* `see`: a qué otros datos apunta la regla, para que quien la lee salte ahí (aparece como una fila **See also** de botones). Es una lista
  de textos, cada uno una **categoría** (`kin`, `professions`, `skills`, `abilities`, `spells`, `weapons`, `armor`, `gear`, `creatures`:
  abre esa lista), la **clave de una entrada** (`core/kin/human`, `core/profession/thief`, `core/skill/languages`: abre la ficha) o la
  **clave de otra regla** (`core/rule/melee-combat`). Un `see` que no apunta a nada se avisa al cargar el pack. Así una regla
  puede decir «elegí un kin» y llevar a la lista de Kin, que vive en otro JSON.
* **Cualquier otra clave** de una regla se conserva como dato para el programa, como en las fichas (`"step": "4"`, `"trained_skills": 6`,
  `"attribute_mods": { "AGL": 1 }`, `"optional": true`): el creador de personajes los lee de ahí. Texto y números tal cual; objetos y listas,
  como su JSON. `step` además se muestra delante del título («4. Age») en Rules.
* Core se carga siempre primero, así que sus reglas existen cuando el homebrew las nombra. Apagar el pack de homerules
  deja las reglas del libro como estaban. El árbol admite hasta 24 niveles.

Un ejemplo es **Character Creation** en Core: los trece pasos del libro en orden (`step` del 1 al 13), cada uno con su texto, sus tablas
y un `see` hacia donde salen sus opciones, y con los datos que el creador necesita (por ejemplo, `Young`, `Adult` y `Old` dicen cuántas
habilidades entrenadas dan y cómo cambian los atributos). Sirve como referencia para el máster y como base del creador de personajes.

## Cómo se combina con el resto

* La búsqueda (`Ctrl+K`) cubre todos los packs activos; las criaturas, hechizos, etc. del homebrew se listan en sus mosaicos
  junto a los del Core, con su fuente.
* Cada entrada tiene una **clave estable** `<pack>/<tipo>/<id>` (`frostmarch/spell/rime-ward`). Los personajes, el
  encuentro y los «recientes» se guardan con esas claves, no con números: apagar, actualizar o reordenar un pack no los rompe.
  Si un pack se quita, las fichas siguen mostrando el nombre que guardaron.
* Cambiar el `id` de una entrada en una versión nueva del pack rompe el vínculo con los personajes que ya la usan: conviene
  fijar los `id` desde el principio.
* Límites: 64 MB por archivo JSON, 5 000 archivos y 512 MB por pack; solo se importan `.json .png .jpg .jpeg .gif .bmp .txt .md`.
  Un `.zip` con rutas que intenten salir de la carpeta (`../`) se rechaza.
