#ifndef __F_KGDB_H
#define __F_KGDB_H

ssize_t kgdb_write(char  *buf, size_t count);
ssize_t kgdb_read(char  *buf, size_t count);

#endif /* __F_KGDB_H */
