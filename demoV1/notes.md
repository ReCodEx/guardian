konzultace:

    1. cast - zatim asi necham na pozdeji

    rozdeleni 2 casti na podkapitoly, 
    styl psani technickych detailu, (vzorky kodu + popis) - detaily na levelu syscallu popisovat 

    co patri do bakalarky a co do dokumentace?,

    demo
    co dal - 

    vyresit problemy se spustenim - 
    jak ma vypadat rozhrani ReCoDex isolate - sepsat co isolate dela,
    keeper daemon proces? - prirazovani ID, directory, cgroup - zatim neresit, vyresit zkontrolovanim existence
    
	TODO: special options v isolate
	
konzultace cca 1.5.

zeptat se na CONSULT polozky z rozhrani
syntax konfiguraku a meta souboru

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

- CLONE_INTO_CGROUP - ano/ne

- moznost castecne obejit disk quota pomoci limitu na fds a velikost souboru.

- diskuze o prirazovani UID/GID - nastudovat v isolate
-dynamic linker chyba pri nizkem limitu na pocet deskriptoru

- zminka o user namespacech, k cemu by se v nasem pripade hodily.
