# Terminal commands

Open **Terminal** from the Window menu. Enter `help` to list commands.
Press Enter or Execute to run a command, Tab to complete a command name,
and Up/Down to recall history. Type `/` to show command suggestions and keep
typing to filter them (for example `/entity.`). Use Up/Down to select a suggestion,
then Tab or Enter to insert it, or click a suggestion. Enter again after filling
in the arguments to execute. Commands accept an optional leading `/`.

Entity targets accept an exact name, a quoted name containing spaces, the
`#` followed by 32 hexadecimal digits printed by `entity.list`, or `@selected`
(the primary selection). Duplicate names require an ID. Names and IDs are case sensitive.

| Command | Action |
| --- | --- |
| `entity.list [name-filter]` | List IDs and names; `*` marks selected entities. |
| `entity.create <kind> [name]` | Create and select an Entity, Model, Camera, or DirectionalLight. |
| `entity.select <target>` | Replace the editor selection. |
| `entity.info <target>` | Show reflected components, property IDs, and values. |
| `entity.rename <target> <name>` | Change the name. |
| `entity.set <target> <Component.property> <value...>` | Edit one reflected property. |
| `entity.add <target> <Component>` | Add an optional reflected component and select the target. |
| `entity.parent <target> <parent\|@root>` | Change the parent, preserving the local transform. |
| `entity.delete <target>` | Delete the target; children become roots. |
| `editor.undo` / `editor.redo` | Undo or redo an edit, including entity operations. |
| `echo <text>` | Print text. |
| `log.clear` | Clear Output Log. |

```text
entity.create Entity "My Entity"
entity.set @selected Transform.translation 1 2 3
entity.set @selected Transform.scale 2 2 2
entity.set @selected Transform.rotation 0 0 0 1
entity.add @selected Camera
entity.info @selected
entity.rename @selected "Main Camera"
editor.undo
```

Property names and formats come from the same reflection registry as Details.
Strings containing spaces need double quotes; embedded quotes and backslashes
inside quoted strings use `\"` and `\\`. Booleans use `true`/`false`, enum values
use their integer value, and vectors use space separated components. Rotation
uses a quaternion in x y z w order. Invalid values are rejected by the existing
property validators. A command targets one entity; `@selected` uses only the primary selection.

Edits share the editor's history and unsaved state. They are unavailable during
Play and while a document confirmation or Save As request is pending.
