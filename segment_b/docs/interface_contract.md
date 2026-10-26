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

Numérotation fixée à J1 (stable). La sémantique précise est implémentée
par B ; les cases ci-dessous sont des placeholders à compléter.

| Nom              | Code hex | Argument (payload)      | Retour (data)            |
| ---------------- | -------- | ----------------------- | ------------------------ |
| `CMD_PING`       | `0x01`   | aucun                   | _(à compléter)_          |
| `CMD_GET_VERSION`| `0x02`   | aucun                   | chaîne de version        |
| `CMD_SET_FLAG`   | `0x03`   | _(à compléter)_         | _(à compléter)_          |
| `CMD_CLEAR_FLAG` | `0x04`   | _(à compléter)_         | _(à compléter)_          |

Codes de statut (`cmd_response.status`) :

| Nom                 | Valeur | Sens                                   |
| ------------------- | ------ | -------------------------------------- |
| `CMD_STATUS_OK`     | `0`    | commande traitée avec succès           |
| `CMD_STATUS_ERR`    | `1`    | commande reconnue mais échouée         |
| `CMD_STATUS_UNKNOWN`| `2`    | identifiant de commande non reconnu    |

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
- Aucun hook de l'appel système `kill` côté B (contrainte sujet) : C garde
  la main sur le choix des appels système remplacés.

**Ce que D attend de B :**

- Le header `protocol.h` partagé, compilable en espace utilisateur.
- Le nœud `/dev/companion` accessible en lecture/écriture (root).
- Le contrat `write()` (envoi requête) → `read()` (lecture réponse).
