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
│   ├── protocol.h        # format d'échange partagé noyau <-> userland
│   ├── companion_dev.h   # API publique du canal de commande
│   └── syscall_hook.h    # API publique du framework de hook ftrace
├── src/
│   ├── module_core.c     # init/exit, cycle de vie, auto-test de hook
│   ├── companion_dev.c   # misc device /dev/companion + file_operations
│   └── syscall_hook.c    # framework de remplacement de syscall (ftrace)
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
- **remplacement de syscall** : `syscall_hook.c` fournit un framework de hook
  générique basé sur ftrace (`fh_install_hooks` / `fh_remove_hooks`) que le
  segment C réutilise pour sa furtivité. B n'installe qu'un auto-test inerte,
  désactivé par défaut. Jamais de hook sur `kill`.

### Auto-test du mécanisme de hook

```sh
insmod rootkit_b.ko selftest=1   # installe un hook transparent sur getpid
dmesg | tail                     # "hooked __x64_sys_getpid"
rmmod rootkit_b                  # "self-test hook removed (N calls)"
```

Le hook appelle l'original et renvoie sa valeur inchangée : aucun effet
visible, il prouve seulement que le remplacement fonctionne. Sans
`selftest=1`, B n'installe aucun hook.

## État du protocole

La chaîne de commande est implémentée dans `companion_dev.c` :

- `companion_write()` — copie bornée (`copy_from_user`), validation de
  `len`/`arg_len`, dispatch sur `cmd_request.command`, mise en file de la
  réponse.
- `companion_read()` — restitue la `cmd_response` préparée (lectures
  partielles supportées).
- Commandes : `CMD_PING`, `CMD_GET_VERSION`, `CMD_SET_FLAG`,
  `CMD_CLEAR_FLAG` ; commande inconnue → `CMD_STATUS_UNKNOWN`.

Voir `docs/interface_contract.md` pour le détail des arguments et retours.

### Test rapide depuis l'espace utilisateur (en root dans la VM)

La réponse est mise en file **par descripteur ouvert** : il faut écrire la
requête et lire la réponse sur le **même** fd (write puis read sur deux
ouvertures séparées perd la réponse).

```sh
# CMD_PING : command=0x01, arg_len=0 -> 8 octets d'en-tête (little-endian)
exec 3<>/dev/companion
printf '\x01\x00\x00\x00\x00\x00\x00\x00' >&3
dd bs=64 count=1 <&3 2>/dev/null | od -An -tx1   # -> status=0, data_len=0
exec 3>&-
```

Pour `CMD_GET_VERSION`, remplacer l'octet de commande par `\x02` : la
réponse porte `status=0`, `data_len=5` puis `"0.1.0"`.

(Le compagnon du segment D fournira l'outil propre pour sérialiser les
requêtes ; la commande ci-dessus n'est qu'une vérification manuelle.)

## TODO restant

- `companion_ioctl()` — laissé en `-ENOTTY`. À définir seulement si un
  chemin ioctl (magic number, `_IOW`/`_IOR`) s'avère nécessaire.
- Enrichir le jeu de commandes (codes ≥ `0x05`) selon le catalogue décidé
  avec D ; brancher la logique métier réelle derrière les flags.
- Mécanisme de remplacement d'appels système (autre livrable du segment B,
  support des hooks de C) — hors de ce fichier.

Contraintes à respecter : identifiants en anglais, style C ANSSI, aucune
ressource fuitée (tout ce qui est alloué dans `init`/`open` est libéré dans
`exit`/`release`), pas de hook de l'appel système `kill`.
