# Segment B — Batterie de tests

Deux parties : **A. Mise en place** (build + déploiement dans la VM), puis
**B. Tests** (chargement, protocole, hook ftrace, stabilité). Les commandes
marquées `(hôte)` se lancent sur ta machine, celles marquées `(VM)` dans la
VM QEMU, en **root**.

Rappels de chemins : `linux-7.2.6/` (arbre noyau) et `disk.img` sont à côté de
`Rootkit/` ; les sources sont dans `Rootkit/segment_b/`.

---

## A. Mise en place

### A.1 — Vérifier la config du noyau (prérequis du hook ftrace)

Le framework de remplacement de syscall exige ces options. À vérifier **avant**
de perdre du temps : si elles manquent, le hook ne se chargera pas.

`(hôte)` dans l'arbre noyau :
```bash
grep -E 'CONFIG_(KPROBES|FUNCTION_TRACER|DYNAMIC_FTRACE|DYNAMIC_FTRACE_WITH_REGS|KALLSYMS)=' linux-7.2.6/.config
```
Attendu : chaque ligne à `=y`. Si `CONFIG_DYNAMIC_FTRACE_WITH_REGS` ou
`CONFIG_KPROBES` manque, voir avec le titulaire A pour recompiler le noyau
(le canal de commande, lui, fonctionne sans ces options).

### A.2 — Compiler le module

`(hôte)` depuis `Rootkit/segment_b/` :
```bash
make KDIR=../../linux-7.2.6
```
Attendu : `rootkit_b.ko` produit, zéro warning (le noyau compile en `-Werror`).

### A.3 — Rendre le module accessible dans la VM

**Option 1 — partage 9p (recommandé, pas de remontage d'image).**
`(hôte)` au lancement de QEMU, ajouter :
```
-virtfs local,path=Rootkit/segment_b,mount_tag=host,security_model=none
```
`(VM)` une fois bootée :
```sh
mount -t 9p -o trans=virtio host /mnt && cd /mnt
```

**Option 2 — injecter dans `disk.img` (image éteinte).**
`(hôte)` :
```bash
sudo losetup -Pf --show disk.img         # -> /dev/loopN
sudo mount /dev/loopNp1 /mnt
sudo cp rootkit_b.ko tests/test_protocol.sh /mnt/root/
sudo umount /mnt && sudo losetup -d /dev/loopN
```

### A.4 — Démarrer la VM

`(hôte)` (commande du TP01) :
```bash
qemu-system-x86_64 -hda disk.img -nographic
```
Login **root**. Pour quitter QEMU : `Ctrl-a` puis `x`.

---

## B. Tests

### B.1 — Chargement et device

`(VM)` :
```sh
insmod rootkit_b.ko
dmesg | tail
ls -l /dev/companion
```
Attendu dans `dmesg` :
```
rootkit_b: init
companion: registered as /dev/companion (minor <n>)
rootkit_b: ready
```
Attendu pour le device : `crw------- 1 root root ... /dev/companion` (mode `0600`).

### B.2 — Protocole (automatisé)

`(VM)`, avec le module chargé :
```sh
sh test_protocol.sh
```
Attendu : `== Resultat : 6 OK, 0 KO ==`. Le script couvre PING, GET_VERSION,
SET_FLAG/CLEAR_FLAG, flag hors borne (ERR) et commande inconnue (UNKNOWN).

### B.3 — Protocole (manuel, pour comprendre)

PING sur un même descripteur ouvert :
```sh
exec 3<>/dev/companion
printf '\x01\x00\x00\x00\x00\x00\x00\x00' >&3
dd bs=64 count=1 <&3 2>/dev/null | od -An -tx1
exec 3>&-
```
Attendu : `00 00 00 00 00 00 00 00` (status=0, data_len=0).

GET_VERSION : remplacer le 1er octet par `\x02` → attendu
`00 00 00 00 05 00 00 00 30 2e 31 2e 30` (status=0, data_len=5, `0.1.0`).

### B.4 — Robustesse (cas d'erreur, ne doit pas planter)

Ces écritures doivent être **rejetées** par `write()`, pas planter le noyau.

Écriture trop courte (< 8 octets) → erreur `EINVAL` :
```sh
printf '\x01\x00\x00' > /dev/companion ; echo "exit=$?"
```
Attendu : un message d'erreur d'écriture et `exit=1` (non nul). Rien dans
`dmesg` d'autre que la ligne `companion: write (len=3)`.

`arg_len` incohérent (annonce 99 octets, en envoie 0) → `EINVAL` :
```sh
printf '\x01\x00\x00\x00\x63\x00\x00\x00' > /dev/companion ; echo "exit=$?"
```
Attendu : `exit` non nul, pas de plantage.

Deux ouvertures séparées (write puis read) → la 2e lecture rend 0, c'est voulu :
```sh
printf '\x01\x00\x00\x00\x00\x00\x00\x00' > /dev/companion
dd if=/dev/companion bs=64 count=1 2>/dev/null | od -An -tx1
```
Attendu : aucune sortie (0 octet lu). La réponse était attachée au 1er
descripteur, fermé entre-temps.

### B.5 — Mécanisme de remplacement de syscall (hook ftrace)

Décharger puis recharger avec l'auto-test activé :
```sh
rmmod rootkit_b
insmod rootkit_b.ko selftest=1
dmesg | tail
```
Attendu dans `dmesg` :
```
syscall_hook: hooked __x64_sys_getpid
rootkit_b: self-test hook installed
rootkit_b: ready
```
Si tu vois à la place une erreur `register_kprobe failed` ou
`ftrace_set_filter_ip failed`, c'est une option noyau manquante (revenir à A.1).

Le hook est **transparent** (aucun effet visible) — il compte juste les appels.
Générer quelques appels à `getpid`, puis décharger :
```sh
sh -c ':'            # quelques process -> quelques getpid
id >/dev/null
rmmod rootkit_b
dmesg | tail
```
Attendu : `rootkit_b: self-test hook removed (N calls)` avec `N > 0`.

### B.6 — Cycle de vie (fuites / stabilité)

Enchaîner plusieurs charge/décharge ne doit rien casser :
```sh
for i in 1 2 3 4 5; do insmod rootkit_b.ko && rmmod rootkit_b; done
dmesg | tail -n 20
```
Attendu : 5 cycles `init … ready` / `exit … unloaded` propres, aucun warning,
aucune trace `kmemleak`/oops. Le device `/dev/companion` doit disparaître après
chaque `rmmod` (`ls -l /dev/companion` → absent).

---

## Récapitulatif

| Test | Couvre | Attendu |
|------|--------|---------|
| B.1  | chargement, device | `registered`, mode `0600` |
| B.2  | protocole complet | `6 OK, 0 KO` |
| B.3  | protocole manuel | octets de réponse corrects |
| B.4  | entrées malformées | rejet `EINVAL`, pas de crash |
| B.5  | hook ftrace | `hooked __x64_sys_getpid`, `N calls` |
| B.6  | cycle de vie | 5 cycles propres, device retiré |
