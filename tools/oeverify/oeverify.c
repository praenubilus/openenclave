// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#include <errno.h>
#include <openenclave/attestation/sgx/evidence.h>
#include <openenclave/attestation/verifier.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const oe_uuid_t oe_format_default = {OE_FORMAT_UUID_SGX_ECDSA};

typedef struct _evidence_uuid_desc
{
    const char* name;
    oe_uuid_t uuid;
} _evidence_uuid_desc;

#define OE_REPORT_FORMATS_MAP(XX) \
    XX(SGX_ECDSA)                 \
    XX(LEGACY_REPORT_REMOTE)      \
    XX(RAW_SGX_QUOTE_ECDSA)       \
    XX(SGX_LOCAL_ATTESTATION)     \
    XX(SGX_EPID_LINKABLE)         \
    XX(SGX_EPID_UNLINKABLE)

static char* allowed_report_formats =
#define XX(id) #id " "
    OE_REPORT_FORMATS_MAP(XX)
#undef XX
    ;

static const _evidence_uuid_desc valid_report_formats[] = {
#define XX(id) {#id, {OE_FORMAT_UUID_##id}},
    OE_REPORT_FORMATS_MAP(XX)
#undef XX
};

size_t get_filesize(FILE* fp)
{
    size_t size = 0;
    fseek(fp, 0, SEEK_END);
    size = (size_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    return size;
}

bool read_binary_file(
    const char* filename,
    uint8_t** data_ptr,
    size_t* size_ptr)
{
    size_t size = 0;
    uint8_t* data = NULL;
    size_t bytes_read = 0;
    bool result = false;
    FILE* fp = NULL;
#ifdef _WIN32
    if (fopen_s(&fp, filename, "rb") != 0)
#else
    if (!(fp = fopen(filename, "rb")))
#endif
    {
        fprintf(stderr, "Failed to open: %s\n", filename);
        goto exit;
    }

    *data_ptr = NULL;
    *size_ptr = 0;

    // Find file size
    size = get_filesize(fp);
    if (size == 0)
    {
        fprintf(stderr, "Empty file: %s\n", filename);
        goto exit;
    }

    data = (uint8_t*)malloc(size);
    if (data == NULL)
    {
        fprintf(
            stderr,
            "Failed to allocate memory of size %lu\n",
            (unsigned long)size);
        goto exit;
    }

    bytes_read = fread(data, sizeof(uint8_t), size, fp);
    if (bytes_read != size)
    {
        fprintf(stderr, "Failed to read file: %s\n", filename);
        goto exit;
    }

    result = true;

exit:
    if (fp)
    {
        fclose(fp);
    }

    if (!result)
    {
        if (data != NULL)
        {
            free(data);
            data = NULL;
        }
        bytes_read = 0;
    }

    *data_ptr = data;
    *size_ptr = bytes_read;

    return result;
}

oe_result_t verify_report(
    const char* report_filename,
    const char* endorsement_filename,
    oe_uuid_t format)
{
    oe_result_t result = OE_FAILURE;
    size_t report_file_size = 0;
    uint8_t* report_data = NULL;
    size_t endorsement_file_size = 0;
    uint8_t* endorsement_data = NULL;
    oe_claim_t* claims = NULL;
    size_t claims_length = 0;

    if (read_binary_file(report_filename, &report_data, &report_file_size))
    {
        if (endorsement_filename != NULL)
        {
            read_binary_file(
                endorsement_filename,
                &endorsement_data,
                &endorsement_file_size);
        }

        oe_verifier_initialize();
        result = oe_verify_evidence(
            &format,
            report_data,
            report_file_size,
            endorsement_data,
            endorsement_file_size,
            NULL,
            0,
            &claims,
            &claims_length);
    }

    printf("Printing Claim Names(Claim value size)\n");
    for (size_t j = 0; j < claims_length; j++)
    {
        printf("%s(%zu) \n", claims[j].name, claims[j].value_size);
    }

    if (report_data != NULL)
    {
        free(report_data);
    }

    if (endorsement_data != NULL)
    {
        free(endorsement_data);
    }

    return result;
}

oe_result_t sgx_enclave_claims_verifier(
    oe_claim_t* claims,
    size_t claims_length,
    void* arg)
{
    oe_result_t result = OE_VERIFY_FAILED;

    (void)arg;
    printf("sgx_enclave_claims_verifier is called with claims:\n");

    for (size_t i = 0; i < claims_length; i++)
    {
        oe_claim_t* claim = &claims[i];
        if (strcmp(claim->name, OE_CLAIM_SECURITY_VERSION) == 0)
        {
            uint32_t security_version = *(uint32_t*)(claim->value);
            // Check the enclave's security version
            if (security_version < 1)
            {
                printf(
                    "identity->security_version checking failed (%d)\n",
                    security_version);
                goto done;
            }
        }
        // Dump an enclave's unique ID, signer ID and Product ID. They are
        // MRENCLAVE, MRSIGNER and ISVPRODID for SGX enclaves. In a real
        // scenario, custom id checking should be done here
        else if (
            strcmp(claim->name, OE_CLAIM_SIGNER_ID) == 0 ||
            strcmp(claim->name, OE_CLAIM_UNIQUE_ID) == 0 ||
            strcmp(claim->name, OE_CLAIM_PRODUCT_ID) == 0)
        {
            printf("Enclave %s:\n", claim->name);
            for (size_t j = 0; j < claim->value_size; j++)
            {
                printf("0x%0x ", claim->value[j]);
            }
        }
    }

    result = OE_OK;
done:
    return result;
}

oe_result_t verify_cert(const char* filename)
{
    oe_result_t result = OE_FAILURE;
    size_t cert_file_size = 0;
    uint8_t* cert_data = NULL;

    if (read_binary_file(filename, &cert_data, &cert_file_size))
    {
        oe_verifier_initialize();
        result = oe_verify_attestation_certificate_with_evidence(
            cert_data, cert_file_size, sgx_enclave_claims_verifier, NULL);
    }

    if (cert_data != NULL)
    {
        free(cert_data);
    }

    return result;
}

bool get_report_format(const char* format_name, oe_uuid_t* format)
{
    bool format_is_known = false;
    for (unsigned int i = 0;
         i < (sizeof(valid_report_formats) / sizeof(valid_report_formats[0]));
         i++)
    {
        if (strcmp(format_name, valid_report_formats[i].name) == 0)
        {
            *format = valid_report_formats[i].uuid;
            format_is_known = true;
        }
    }

    return format_is_known;
}

void print_allowed_formats()
{
    fprintf(
        stdout, "Allowed report formats are: [ %s]\n", allowed_report_formats);
}

void print_syntax(const char* program_name)
{
    fprintf(
        stdout,
        "Usage:\n  %s -r <report_file> [-e <endorsement_file>] [-f "
        "<report_format>] \n  %s -c "
        "<certificate_file>\n",
        program_name,
        program_name);
    print_allowed_formats();
    fprintf(
        stdout,
        "\nVerify the integrity of enclave attestation evidence or attestation "
        "certificate.\n");
}

int main(int argc, const char* argv[])
{
    const char* report_filename = NULL;
    const char* endorsement_filename = NULL;
    const char* certificate_filename = NULL;
    const char* report_format = NULL;
    oe_result_t result = OE_FAILURE;
    int n = 0;

    if (argc <= 2)
    {
        print_syntax(argv[0]);

        if (argc == 2 && memcmp(argv[1], "-h", 2) == 0)
        {
            return 0;
        }

        return 1;
    }

    for (n = 1; n < argc; n++)
    {
        if (memcmp(argv[n], "-r", 2) == 0)
        {
            if (argc > (n - 1))
                report_filename = argv[++n];
        }
        else if (memcmp(argv[n], "-e", 2) == 0)
        {
            if (argc > (n - 1))
                endorsement_filename = argv[++n];
        }
        else if (memcmp(argv[n], "-c", 2) == 0)
        {
            if (argc > (n - 1))
                certificate_filename = argv[++n];
        }
        else if (memcmp(argv[n], "-f", 2) == 0)
        {
            if (argc > (n - 1))
            {
                report_format = argv[++n];
            }
        }
        else
        {
            print_syntax(argv[0]);
            return 1;
        }
    }

    if (report_filename == NULL && certificate_filename == NULL)
    {
        print_syntax(argv[0]);
        return 1;
    }
    else
    {
        oe_uuid_t oe_format = oe_format_default;

        if (report_format && !get_report_format(report_format, &oe_format))
        {
            fprintf(
                stderr,
                "Error: Format report \"%s\" is unknown\n",
                report_format);
            print_allowed_formats();
            return 1;
        }

        if (report_filename != NULL)
        {
            fprintf(stdout, "Verifying report %s...\n", report_filename);
            result =
                verify_report(report_filename, endorsement_filename, oe_format);
            fprintf(
                stdout,
                "Report verification %s (%u).\n\n",
                (result == OE_OK) ? "succeeded" : "failed",
                result);
        }

        if (certificate_filename != NULL)
        {
            fprintf(
                stdout, "Verifying certificate %s...\n", certificate_filename);
            result = verify_cert(certificate_filename);
            fprintf(
                stdout,
                "\n\nCertificate verification %s (%u).\n\n",
                (result == OE_OK) ? "succeeded" : "failed",
                result);
        }
    }

    return 0;
}
