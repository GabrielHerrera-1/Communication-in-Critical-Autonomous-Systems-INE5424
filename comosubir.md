## Etapa 1: Prova de Conceito (Raw Sockets)

O arquivo `rede_hello.cpp` é o nosso "Hello World" de rede. Ele prova que conseguimos enviar e receber frames Ethernet diretamente na camada de enlace usando um endereço de Broadcast (`FF:FF:FF:FF:FF:FF`) na rede virtual do QEMU.

### ⚠️ Requisitos Prévios

Para testar, você precisará colar na raiz deste repositório os arquivos binários pesados fornecidos pelo professor (eles foram ignorados pelo Git):
- `Image` (O Kernel do Linux pré-compilado para RISC-V)
- `initramfs.cpio` (O sistema de arquivos base da VM)

Você também precisará das ferramentas de compilação e emulação no seu Linux:
```
sudo apt install g++-riscv64-linux-gnu qemu-system-misc cpio
```

### Passo a Passo para Execução Manual

**1. Compilar o código fonte**
Precisamos compilar o programa de forma estática para a arquitetura da máquina virtual (RISC-V):
```
riscv64-linux-gnu-g++ -static rede_hello.cpp -o rede_hello
```

**2. Injetar o programa no sistema de arquivos da VM**
Vamos extrair o `.cpio` original, colocar nosso programa dentro e reempacotar em um novo arquivo:
```
mkdir work && cd work
cpio -id < ../initramfs.cpio
cp ../rede_hello .
find . | cpio -o -H newc > ../novo_initramfs.cpio
cd ..
rm -rf work/
```

**3. Iniciar a VM 1 (Receptor / Escuta)**
Abra um terminal e rode o QEMU configurando o MAC Address com final `01`. Esta máquina ficará escutando a rede:
```
qemu-system-riscv64 -machine virt -nographic -kernel Image -initrd novo_initramfs.cpio -netdev socket,id=net0,mcast=230.0.0.1:1234 -device virtio-net-device,netdev=net0,mac=52:54:00:00:00:01 --append "root=/dev/ram"
```
*Dentro da VM iniciada, digite e execute:* 
```
/rede_hello
```

**4. Iniciar a VM 2 (Emissor / Grito)**
Abra um **segundo terminal** (mantenha o primeiro aberto) e rode o QEMU configurando o MAC Address com final `02` para não dar conflito:
```
qemu-system-riscv64 -machine virt -nographic -kernel Image -initrd novo_initramfs.cpio -netdev socket,id=net0,mcast=230.0.0.1:1234 -device virtio-net-device,netdev=net0,mac=52:54:00:00:00:02 --append "root=/dev/ram"
```
*Dentro da VM iniciada, digite e execute:* 
```
/rede_hello
```

Neste momento, verifique o terminal da **VM 1**. A mensagem enviada pela VM 2 deve aparecer na tela!

> **Dica:** Para sair do emulador QEMU e voltar ao seu terminal normal, pressione `Ctrl+A` e em seguida a tecla `X`.

