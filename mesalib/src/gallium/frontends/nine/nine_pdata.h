
#ifndef _NINE_PDATA_H_
#define _NINE_PDATA_H_

struct pheader
{
    boolean unknown;
    GUID guid;
    DWORD size;
};

static bool
ht_guid_compare( const void *a,
                 const void *b )
{
    return GUID_equal(a, b);
}

static uint32_t
ht_guid_hash( const void *key )
{
    unsigned i, hash = 0;
    const unsigned char *str = key;

    for (i = 0; i < sizeof(GUID); i++) {
        hash = (unsigned)(str[i]) + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

static enum pipe_error
ht_guid_delete( void *key,
                void *value,
                void *data )
{
    struct pheader *header = value;
    void *header_data = (void *)header + sizeof(*header);

    if (header->unknown) { IUnknown_Release(*(IUnknown **)header_data); }
    FREE(header);

    return PIPE_OK;
}

#endif /* _NINE_PDATA_H_ */
