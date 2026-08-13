#ifndef CONFIG_H
#define CONFIG_H

#define PACKAGES_JSON  "/etc/npkg/packages/"      /* Here come all the packages.json */
#define MAKE_CONF      "/etc/npkg/make.conf"      /* .conf file for npkg compilling */
#define INSTALLED_DIR  "/var/lib/npkg/installed/" /* One manifest per installed package, <name>.json */
#define BUILD_DIR      "/var/cache/npkg/build/"   /* Directory to use DESTDIR  */

#define NPKG_LOCK_PATH  "/run/npkg.lock"          /* Path to the lock for prevent two npkg instances  */
#define NPKG_SAFE_PATH "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" /* Secure path to execute extern programs */
#define NPKG_SAFE_PKG_CONFIG_PATH "/usr/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig" /* Secure path to find .so in pkg-config  */

#endif
