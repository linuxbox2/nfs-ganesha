/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file  FSAL/fsal_convert.c
 * @brief FSAL type translation functions.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "fsal_convert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define MAX_2( x, y )    ( (x) > (y) ? (x) : (y) )
#define MAX_3( x, y, z ) ( (x) > (y) ? MAX_2((x),(z)) : MAX_2((y),(z)) )

/**
 * fsal2posix_testperm:
 * Convert FSAL permission flags to Posix permission flags.
 *
 * \param testperm (input):
 *        The FSAL permission flags to be tested.
 *
 * \return The POSIX permission flags to be tested.
 */
int fsal2posix_testperm(fsal_accessflags_t testperm)
{

  int posix_testperm = 0;

  if(testperm & FSAL_R_OK)
    posix_testperm |= R_OK;
  if(testperm & FSAL_W_OK)
    posix_testperm |= W_OK;
  if(testperm & FSAL_X_OK)
    posix_testperm |= X_OK;
  if(testperm & FSAL_F_OK)
    posix_testperm |= F_OK;

  return posix_testperm;

}

/* mode bits are a uint16_t and chmod masks off type
 */

#define S_IALLUGO (~S_IFMT & 0xFFFF)
/**
 * fsal2unix_mode:
 * Convert FSAL mode to posix mode.
 *
 * \param fsal_mode (input):
 *        The FSAL mode to be translated.
 *
 * \return The posix mode associated to fsal_mode.
 */
mode_t fsal2unix_mode(uint32_t fsal_mode)
{
  return fsal_mode &  S_IALLUGO;
}

/**
 * unix2fsal_mode:
 * Convert posix mode to FSAL mode.
 *
 * \param unix_mode (input):
 *        The posix mode to be translated.
 *
 * \return The FSAL mode associated to unix_mode.
 */
uint32_t unix2fsal_mode(mode_t unix_mode)
{
  return unix_mode & S_IALLUGO;
}

/**
 * posix2fsal_type:
 * Convert posix object type to an object type.
 *
 * \param posix_type_in (input):
 *        The POSIX object type.
 *
 * \return - The FSAL node type associated to posix_type_in.
 *         - -1 if the input type is unknown.
 */
object_file_type_t posix2fsal_type(mode_t posix_type_in)
{

  switch (posix_type_in & S_IFMT)
    {
    case S_IFIFO:
      return FIFO_FILE;

    case S_IFCHR:
      return CHARACTER_FILE;

    case S_IFDIR:
      return DIRECTORY;

    case S_IFBLK:
      return BLOCK_FILE;

    case S_IFREG:
    case S_IFMT:
      return REGULAR_FILE;

    case S_IFLNK:
      return SYMBOLIC_LINK;

    case S_IFSOCK:
      return SOCKET_FILE;

    default:
      LogWarn(COMPONENT_FSAL, "Unknown object type: %d", posix_type_in);
      return -1;
    }

}

fsal_fsid_t posix2fsal_fsid(dev_t posix_devid)
{

  fsal_fsid_t fsid;

  fsid.major = posix_devid;
  fsid.minor = 0;

  return fsid;

}

fsal_dev_t posix2fsal_devt(dev_t posix_devid)
{

  fsal_dev_t dev;

  dev.major = major(posix_devid);
  dev.minor = minor(posix_devid);

  return dev;
}
/** @} */
