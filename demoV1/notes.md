TODO:
    skript na mockup workera - C, python, dotnet/maven - hello world funguje, pridat parametrizovane gcc, maven-repo
    
    
    v konfiguraci se musi nastavovat PATH, LD_LIBRARY_PATH
    GCC DEBUGGING!
        echo '' | gcc -xc -E -v -
        LD_LIBRARY_PATH - spravne gcc directories

    MAVEN - maven-repo/.m2 - treba poradne nainstalovat, potom staci r prava a neni potreba internet
    DOTNET - manualni instalace- skript dotnet-install. pridat detekci verze runtimu a prepisovani latest symlinku do mock scriptu
    DNS - /etc/resolv.conf muze byt symlink napr na /run - pridat do dir rules pokud potrebuju internet

    TESTY NA VIRTUALKACH 
    bob1 - java - nainstalovan maven, java version snizena z 21 na 17 -> funguje







    meta files -- zkopirovat format z result.yml - VYPADA DOBRE
    spravit permissions v box_fs - ROZMYSLET
    
    kapitola o pozadavcich recodexu 
    recodex obecne- seznam environmentu, tisice uzivatelu, hodne variabilni zatez, c# kompilace 
    bezpecnost
    isolate -co dela a co bychom chteli

    jak pojmout kapitolu o linuxu - nechodit do detailu, odkazat se na manpages a strucne popsat
    stejne kapitola o bezpecnosti - odkazat se na praci a napsat strucny souhrn
	
TEMATA + PROBLEMY DO BAKALARKY:

- kolize pri behu vice kontejneru najednou - navrh reseni
-single source of truth pristup s pointery.
- MOUNT_DETACH a busy chyba pri umount() 

- redirectovani stdin a stderr - pomoci freopen, nejdrive err, err to out pomoci dup2()

-directory rules - syntax, bezpecnost, ...

-proc zrovna waitpid pri cekani na task

-klonovani tasku, execve, pracovani s PID.

- DETACH flag pri unmountovani rootu.

-system prerequisites a settings - napriklad swap musi byt vypnuty aby fungoval memory limit.

- CLONE_INTO_CGROUP - ano/ne - ANO potrebuji na inicializaci cg namespace

- moznost castecne obejit disk quota pomoci limitu na fds a velikost souboru.

- diskuze o prirazovani UID/GID - nastudovat v isolate
-dynamic linker chyba pri nizkem limitu na pocet deskriptoru

- zminka o user namespacech, k cemu by se v nasem pripade hodily.

- permissions pri behu vice kontejneru + worker commandu

SECURITY - Martin Mares paper
- schovani isolatoru v /proc namespacu.
