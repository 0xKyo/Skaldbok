# Packs de contenido (homebrew)

Todo el contenido del juego que no son las reglas base —criaturas, hechizos, aptitudes, habilidades, razas, profesiones,
equipo y tablas— vive en **packs**. El contenido de los libros (**Core**) es un pack más, generado por
`tools/export_packs.py`, y el homebrew se suma a él: nunca lo reemplaza. Cada entrada mantiene su **fuente**: lo de los
libros aparece como *Rulebook / Bestiary / Adventure*; lo de un pack de homebrew como *Homebrew · <nombre>*.

Un pack es una **carpeta** (o un **.zip** de esa carpeta) con:

```
mi-pack/
  manifest.json        obligatorio
  creatures.json       un archivo JSON por tipo; todos opcionales
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

## Reglas generales de los archivos

* JSON en UTF-8. Se aceptan comentarios `//` y `/* */` y el BOM que agregan algunos editores de Windows.
* Cada archivo es `{ "spells": [ … ] }` (la clave es el nombre del archivo) o directamente una lista `[ … ]`.
* Los textos largos pueden escribirse como **lista de párrafos**: `"description": ["Primer párrafo.", "Segundo."]`.
* Los números pueden ir como texto (`"rank": "1"`, `"movement": "9"`). Los campos desconocidos se ignoran, así que
  un pack escrito para una versión futura sigue cargando.
* Toda entrada necesita `name`. Sin nombre se **omite** con un aviso; el resto del pack carga igual.
* `id` es opcional (se deriva del nombre). Debe ser único dentro de su archivo; si se repite, se renumera y se avisa.
* `source`, `page`, `printed_page`: ver más abajo.
* `fields`: cualquier ficha (menos criaturas y tablas) acepta `"fields": { "Etiqueta": "valor" }` para mostrar líneas
  extra, sin tocar el código.
* Un pack con JSON inválido, un `manifest.json` roto o hecho para un formato más nuevo **se rechaza entero**, con el
  archivo y la posición del error. Los problemas de una sola entrada nunca rechazan el pack.

## manifest.json

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
| `id` | obligatorio. Minúsculas, dígitos, `-` y `_` (`mi-pack`). Identifica al pack; `core` está reservado |
| `name` | nombre a mostrar (por defecto, el `id`) |
| `version`, `author`, `description` | informativos |
| `sources` | opcional. Los libros o suplementos de donde sale el contenido. Sin esto, hay una fuente con el nombre del pack |

Cada fuente se muestra como **Homebrew · `short`** y tiene su propio botón/filtro (menú *Homebrew* de la barra superior),
su color y su etiqueta en cada tarjeta. Una entrada elige su fuente con `"source": "<key>"`; sin él usa la primera.

### Páginas

`"page": 12` (o `"printed_page"`) es solo una referencia para mostrar (*p.12*): un pack no tiene PDF, así que no hay
botón «Original». Las entradas de los libros (Core) sí llevan enlace a la página del PDF.

## Tipos

Todos los campos son opcionales salvo `name`.

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
`names` (lista de nombres típicos: el creador de personajes los ofrece).

### professions.json — profesiones
* `key_attribute`, `description`.
* `skills`: las habilidades entre las que se eligen las 6 de profesión.
* `heroic_abilities`: aptitudes heroicas entre las que se elige la inicial (de cualquier pack).
* `starting_gear`: los conjuntos de equipo inicial (típicamente 3), como texto. Dentro de un conjunto se separa con comas;
  `A/B/C` es una elección; `D6 food rations` / `D8 silver` se tiran solos.
* `nicknames`: apodos típicos.
* Magia: `"skills_by_school": { "Animism": [ … ], … }` y `"magic": { "spells": 3, "tricks": 3, "spell_rank": 1 }`.
  Si existe `skills_by_school` el creador pide elegir escuela, y la profesión no da aptitud heroica.

### weapons.json, armor.json, gear.json — equipo
* Arma: `kind` (`melee`/`ranged`), `grip`, `str_req`, `range`, `damage`, `durability`, `cost`, `supply`, `features`.
* Armadura: `slot` (`armor` o `helmet`), `armor_rating`, `cost`, `supply`, `effect`.
* Objeto: `category`, `cost`, `supply`, `weight`, `effect`.

Todo el equipo aparece en el módulo **Equipment** y se puede añadir a la ficha de un personaje.

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
listas de celdas). Dos campos opcionales: `"browse": false` la deja fuera de la lista Tables, y
`"role": "weakness" | "memento" | "appearance"` hace que el creador de personajes tire en ella (además de las del Core).

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
