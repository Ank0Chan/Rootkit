# Segment B — Cœur noyau et canal de commande

Ossature du LKM et canal de communication entre l'espace utilisateur
(segment D) et le noyau. Le module se compile en un unique
`rootkit_b.ko`. À J1, les corps de fonctions du protocole sont des
squelettes : le device existe, le format est figé, le décodage reste à
implémenter.

## Structure du segment

```
segment_b/
├── Makefile              # build out-of-tree, cibles all/clean/load/unload/reload/log
├── include/
│   └── protocol.h        # format d'échange partagé noyau <-> userland
├── src/
│   ├── module_core.c     # init/exit, cycle de vie du module
│   └── companion_dev.c   # misc device /dev/companion + file_operations
├── docs/
│   └── interface_contract.md  # contrat d'interface figé à J1
└── README.md
```

## Build & test

À exécuter **dans la VM QEMU/LFS**, pas sur la machine hôte (le module se
compile contre le noyau cible).

```bash
make                # construit rootkit_b.ko
make load           # insmod rootkit_b.ko
make log            # dmesg | tail, vérifie "registered as /dev/companion"
ls -l /dev/companion
make unload         # rmmod rootkit_b
make reload         # unload + load
make clean          # nettoie les artefacts de build
```

Vérification attendue dans `dmesg` après `make load` :

```
rootkit_b: init
companion: registered as /dev/companion (minor <n>)
rootkit_b: ready
```

## Architecture

- **misc device** : `companion_dev.c` enregistre un misc device nommé
  `companion` (minor dynamique, mode `0600`). C'est l'unique canal entre le
  compagnon userland et le noyau. Les `file_operations` (`open`, `release`,
  `read`, `write`, `unlocked_ioctl`) sont les points d'entrée du protocole.
- **cœur du module** : `module_core.c` ne fait qu'orchestrer le cycle de
  vie — `companion_dev_register()` à l'`init`, `companion_dev_unregister()`
  à l'`exit`, avec un chemin d'erreur en `goto` cascade prêt à accueillir
  les futures ressources.
- **protocole** : `protocol.h` définit `cmd_request` (commande + argument)
  et `cmd_response` (statut + données), partagées entre noyau et userland.
  Voir `docs/interface_contract.md` pour le contrat figé.

## TODO (à implémenter par l'étudiant)

Les squelettes suivants sont à compléter dans le corps des fonctions :

- `companion_write()` — copier les octets entrants dans un buffer noyau
  borné, valider `len` et `arg_len`, puis dispatcher sur
  `cmd_request.command`. Rejeter toute requête malformée/surdimensionnée
  avec `-EINVAL`. **Ne jamais faire confiance à `len`/`arg_len` sans
  contrôle de bornes.**
- `companion_read()` — restituer la `cmd_response` préparée par la dernière
  commande au lieu de renvoyer 0.
- `companion_ioctl()` — définir l'espace de commandes ioctl (magic number,
  encodages `_IOW`/`_IOR`) si ce chemin est retenu, sinon le laisser
  renvoyer `-ENOTTY`.
- Implémenter la sémantique de chaque `CMD_*` et remplir le tableau des
  commandes dans `docs/interface_contract.md`.

Contraintes à respecter : identifiants en anglais, style C ANSSI, aucune
ressource fuitée (tout ce qui est alloué dans `init` est libéré dans
`exit`), pas de hook de l'appel système `kill`.
