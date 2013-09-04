#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <fontconfig/fontconfig.h>

FcBool
mkdir_p(const char *dir)
{
    char *parent;
    FcBool ret;

    if (strlen (dir) == 0)
	return FcFalse;
    parent = (char *) FcStrDirname (dir);
    if (!parent)
	return FcFalse;
    if (access (parent, F_OK) == 0)
	ret = mkdir (dir, 0755) == 0 && chmod (dir, 0755) == 0;
    else if (access (parent, F_OK) == -1)
	ret = mkdir_p (parent) && (mkdir (dir, 0755) == 0) && chmod (dir, 0755) == 0;
    else
	ret = FcFalse;
    free (parent);

    return ret;
}

FcBool
unlink_dirs(const char *dir)
{
    DIR *d = opendir (dir);
    struct dirent *e;
    size_t len = strlen (dir);
    char *n = NULL;
    FcBool ret = FcTrue;

    if (!d)
	return FcFalse;
    while ((e = readdir(d)) != NULL)
    {
	size_t l;

	if (strcmp (e->d_name, ".") == 0 ||
	    strcmp (e->d_name, "..") == 0)
	    continue;
	l = strlen (e->d_name) + 1;
	if (n)
	    free (n);
	n = malloc (l + len + 1);
	strcpy (n, dir);
	n[len] = '/';
	strcpy (&n[len + 1], e->d_name);
	if (e->d_type == DT_DIR)
	{
	    if (!unlink_dirs (n))
	    {
		fprintf (stderr, "E: %s\n", n);
		ret = FcFalse;
		break;
	    }
	}
	else
	{
	    if (unlink (n) == -1)
	    {
		fprintf (stderr, "E: %s\n", n);
		ret = FcFalse;
		break;
	    }
	}
    }
    if (n)
	free (n);
    closedir (d);

    if (rmdir (dir) == -1)
    {
	fprintf (stderr, "E: %s\n", dir);
	return FcFalse;
    }

    return ret;
}

int
main(void)
{
    char template[32] = "fontconfig-XXXXXXXX";
    char *tmp = mkdtemp (template);
    size_t len = strlen (tmp), xlen, dlen;
    char xdg[256], confd[256], fn[256], nfn[256], ud[256], nud[256];
    int ret = -1;
    FILE *fp;
    char *content = "<fontconfig></fontconfig>";

    strcpy (xdg, tmp);
    strcpy (&xdg[len], "/.config");
    setenv ("HOME", tmp, 1);
    setenv ("XDG_CONFIG_HOME", xdg, 1);
    xlen = strlen (xdg);
    strcpy (confd, xdg);
    strcpy (&confd[xlen], "/fontconfig");
    dlen = strlen (confd);
    /* In case there are no configuration files nor directory */
    FcInit ();
    if (access (confd, F_OK) == 0)
    {
	fprintf (stderr, "%s unexpectedly exists\n", confd);
	goto bail;
    }
    FcFini ();
    if (!unlink_dirs (tmp))
    {
	fprintf (stderr, "Unable to clean up\n");
	goto bail;
    }
    /* In case there are the user configuration file */
    strcpy (fn, tmp);
    strcpy (&fn[len], "/.fonts.conf");
    strcpy (nfn, confd);
    strcpy (&nfn[dlen], "/fonts.conf");
    if (!mkdir_p (confd))
    {
	fprintf (stderr, "Unable to create a config dir: %s\n", confd);
	goto bail;
    }
    if ((fp = fopen (fn, "wb")) == NULL)
    {
	fprintf (stderr, "Unable to create a config file: %s\n", fn);
	goto bail;
    }
    fwrite (content, sizeof (char), strlen (content), fp);
    fclose (fp);
    FcInit ();
    if (access (nfn, F_OK) != 0)
    {
	fprintf (stderr, "migration failed for %s\n", nfn);
	goto bail;
    }
    FcFini ();
    if (!unlink_dirs (tmp))
    {
	fprintf (stderr, "Unable to clean up\n");
	goto bail;
    }
    /* In case there are the user configuration dir */
    strcpy (ud, tmp);
    strcpy (&ud[len], "/.fonts.conf.d");
    strcpy (nud, confd);
    strcpy (&nud[dlen], "/conf.d");
    if (!mkdir_p (ud))
    {
	fprintf (stderr, "Unable to create a config dir: %s\n", ud);
	goto bail;
    }
    FcInit ();
    if (access (nud, F_OK) != 0)
    {
	fprintf (stderr, "migration failed for %s\n", nud);
	goto bail;
    }
    FcFini ();

    ret = 0;
bail:
    unlink_dirs (tmp);

    return ret;
}
