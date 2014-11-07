/* $Id: pkcs7-verify.cpp $ */
/** @file
 * IPRT - Crypto - PKCS \#7, Verification
 */

/*
 * Copyright (C) 2006-2014 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "internal/iprt.h"
#include <iprt/crypto/pkcs7.h>

#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/crypto/digest.h>
#include <iprt/crypto/pkix.h>
#include <iprt/crypto/store.h>
#include <iprt/crypto/x509.h>

#ifdef IPRT_WITH_OPENSSL
# include "internal/iprt-openssl.h"
# include <openssl/pkcs7.h>
# include <openssl/x509.h>
# include <openssl/err.h>
#endif



#ifdef IPRT_WITH_OPENSSL
static int rtCrPkcs7VerifySignedDataUsingOpenSsl(PCRTCRPKCS7CONTENTINFO pContentInfo, uint32_t fFlags,
                                                 RTCRSTORE hAdditionalCerts, RTCRSTORE hTrustedCerts,
                                                 void const *pvContent, uint32_t cbContent, PRTERRINFO pErrInfo)
{
    /*
     * Verify using OpenSSL.
     */
    int rcOssl;
    unsigned char const *pbRawContent = RTASN1CORE_GET_RAW_ASN1_PTR(&pContentInfo->SeqCore.Asn1Core);
    PKCS7 *pOsslPkcs7 = NULL;
    if (d2i_PKCS7(&pOsslPkcs7, &pbRawContent, RTASN1CORE_GET_RAW_ASN1_SIZE(&pContentInfo->SeqCore.Asn1Core)) == pOsslPkcs7)
    {
        STACK_OF(X509) *pAddCerts = NULL;
        if (hAdditionalCerts != NIL_RTCRSTORE)
            rcOssl = RTCrStoreConvertToOpenSslCertStack(hAdditionalCerts, 0, (void **)&pAddCerts);
        else
        {
            pAddCerts = sk_X509_new_null();
            rcOssl = RT_LIKELY(pAddCerts != NULL) ? VINF_SUCCESS : VERR_NO_MEMORY;
        }
        if (RT_SUCCESS(rcOssl))
        {
            for (uint32_t i = 0; i < pContentInfo->u.pSignedData->Certificates.cItems; i++)
                rtCrOpenSslAddX509CertToStack(pAddCerts, &pContentInfo->u.pSignedData->Certificates.paItems[i]);


            X509_STORE *pTrustedCerts = NULL;
            if (hTrustedCerts != NIL_RTCRSTORE)
                rcOssl = RTCrStoreConvertToOpenSslCertStore(hTrustedCerts, 0, (void **)&pTrustedCerts);
            if (RT_SUCCESS(rcOssl))
            {
                rtCrOpenSslInit();

                BIO *pBioContent = BIO_new_mem_buf((void *)pvContent, cbContent);
                if (pBioContent)
                {
                    uint32_t fOsslFlags = PKCS7_NOCHAIN;
                    fOsslFlags |= PKCS7_NOVERIFY; // temporary hack.
                    if (PKCS7_verify(pOsslPkcs7, pAddCerts, pTrustedCerts, pBioContent, NULL /*out*/, fOsslFlags))
                        rcOssl = VINF_SUCCESS;
                    else
                    {
                        rcOssl = RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_OSSL_VERIFY_FAILED, "PKCS7_verify failed: ");
                        if (pErrInfo)
                            ERR_print_errors_cb(rtCrOpenSslErrInfoCallback, pErrInfo);
                    }
                    BIO_free(pBioContent);
                }
                if (pTrustedCerts)
                    X509_STORE_free(pTrustedCerts);
            }
            else
                rcOssl = RTErrInfoSet(pErrInfo, rcOssl, "RTCrStoreConvertToOpenSslCertStack failed");
            if (pAddCerts)
                sk_X509_pop_free(pAddCerts, X509_free);
        }
        else
            rcOssl = RTErrInfoSet(pErrInfo, rcOssl, "RTCrStoreConvertToOpenSslCertStack failed");
        PKCS7_free(pOsslPkcs7);
    }
    else
        rcOssl = RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_OSSL_D2I_FAILED, "d2i_PKCS7 failed");

    return rcOssl;
}
#endif /* IPRT_WITH_OPENSSL */


/**
 * @callback_method_impl{RTCRPKCS7VERIFYCERTCALLBACK,
 *  Default implementation that checks for the DigitalSignature KeyUsage bit.}
 */
RTDECL(int) RTCrPkcs7VerifyCertCallbackDefault(PCRTCRX509CERTIFICATE pCert, RTCRX509CERTPATHS hCertPaths,
                                               void *pvUser, PRTERRINFO pErrInfo)
{
    /*
     * Check for the digital signature key usage.
     */
    if (   (pCert->TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_KEY_USAGE)
        && !(pCert->TbsCertificate.T3.fKeyUsage & RTCRX509CERT_KEY_USAGE_F_DIGITAL_SIGNATURE))
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_KEY_USAGE_MISMATCH, "fKeyUsage=%#x, missing %#x",
                             pCert->TbsCertificate.T3.fKeyUsage, RTCRX509CERT_KEY_USAGE_F_DIGITAL_SIGNATURE);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{RTCRPKCS7VERIFYCERTCALLBACK,
 * Standard code signing.  Use this for Microsoft SPC.}
 */
RTDECL(int) RTCrPkcs7VerifyCertCallbackCodeSigning(PCRTCRX509CERTIFICATE pCert, RTCRX509CERTPATHS hCertPaths,
                                                   void *pvUser, PRTERRINFO pErrInfo)
{
    /*
     * Check for the digital signature key usage.  Not required to be present.
     */
    if (pCert->TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_KEY_USAGE)
    {
        if (!(pCert->TbsCertificate.T3.fKeyUsage & RTCRX509CERT_KEY_USAGE_F_DIGITAL_SIGNATURE))
            return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_KEY_USAGE_MISMATCH, "fKeyUsage=%#x, missing %#x",
                                 pCert->TbsCertificate.T3.fKeyUsage, RTCRX509CERT_KEY_USAGE_F_DIGITAL_SIGNATURE);
    }

    /*
     * Check the extended key usage bits if present.
     */
    if (!(pCert->TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_EXT_KEY_USAGE))
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_KEY_USAGE_MISMATCH, "No extended key usage certificate attribute.");
    if (!(pCert->TbsCertificate.T3.fExtKeyUsage & RTCRX509CERT_EKU_F_CODE_SIGNING))
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_KEY_USAGE_MISMATCH, "fExtKeyUsage=%#x, missing %#x",
                             pCert->TbsCertificate.T3.fExtKeyUsage, RTCRX509CERT_EKU_F_CODE_SIGNING);
    return VINF_SUCCESS;
}


/**
 * Deals with authenticated attributes.
 *
 * When authenticated attributes are present (checked by caller) we must:
 *  - fish out the content type and check it against the content inof,
 *  - fish out the message digest among and check it against *phDigest,
 *  - compute the message digest of the authenticated attributes and
 *    replace *phDigest with this for the signature verification.
 *
 * @returns IPRT status code.
 * @param   pSignerInfo         The signer info being verified.
 * @param   pSignedData         The signed data.
 * @param   phDigest            On input this is the digest of the content. On
 *                              output it will (on success) be a reference to
 *                              the message digest of the authenticated
 *                              attributes.  The input reference is consumed.
 *                              The caller shall release the output reference.
 * @param   fFlags              Flags.
 * @param   pErrInfo            Extended error info, optional.
 */
static int rtCrPkcs7VerifySignerInfoAuthAttribs(PCRTCRPKCS7SIGNERINFO pSignerInfo, PCRTCRPKCS7SIGNEDDATA pSignedData,
                                                PRTCRDIGEST phDigest, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    /*
     * Scan the attributes and validate the two required attributes
     * (RFC-2315, chapter 9.2, fourth bullet).  Checking that we've got exactly
     * one of each of them is checked by the santiy checker function, so we'll
     * just assert that it did it's job here.
     */
    uint32_t    cContentTypes   = 0;
    uint32_t    cMessageDigests = 0;
    uint32_t    i               = pSignerInfo->AuthenticatedAttributes.cItems;
    while (i-- > 0)
    {
        PCRTCRPKCS7ATTRIBUTE pAttrib = &pSignerInfo->AuthenticatedAttributes.paItems[i];

        if (RTAsn1ObjId_CompareWithString(&pAttrib->Type, RTCR_PKCS9_ID_CONTENT_TYPE_OID) == 0)
        {
            AssertReturn(!cContentTypes, VERR_CR_PKCS7_INTERNAL_ERROR);
            AssertReturn(pAttrib->enmType == RTCRPKCS7ATTRIBUTETYPE_OBJ_IDS, VERR_CR_PKCS7_INTERNAL_ERROR);
            AssertReturn(pAttrib->uValues.pObjIds->cItems == 1, VERR_CR_PKCS7_INTERNAL_ERROR);

            if (RTAsn1ObjId_Compare(&pAttrib->uValues.pObjIds->paItems[0], &pSignedData->ContentInfo.ContentType) != 0)
                return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_CONTENT_TYPE_ATTRIB_MISMATCH,
                                     "Expected content-type %s, found %s",
                                     &pAttrib->uValues.pObjIds->paItems[0], pSignedData->ContentInfo.ContentType.szObjId);
            cContentTypes++;
        }
        else if (RTAsn1ObjId_CompareWithString(&pAttrib->Type, RTCR_PKCS9_ID_MESSAGE_DIGEST_OID) == 0)
        {
            AssertReturn(!cMessageDigests, VERR_CR_PKCS7_INTERNAL_ERROR);
            AssertReturn(pAttrib->enmType == RTCRPKCS7ATTRIBUTETYPE_OCTET_STRINGS, VERR_CR_PKCS7_INTERNAL_ERROR);
            AssertReturn(pAttrib->uValues.pOctetStrings->cItems == 1, VERR_CR_PKCS7_INTERNAL_ERROR);

            if (!RTCrDigestMatch(*phDigest,
                                 pAttrib->uValues.pOctetStrings->paItems[0].Asn1Core.uData.pv,
                                 pAttrib->uValues.pOctetStrings->paItems[0].Asn1Core.cb))
            {
                size_t cbHash = RTCrDigestGetHashSize(*phDigest);
                if (cbHash != pAttrib->uValues.pOctetStrings->paItems[0].Asn1Core.cb)
                    return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_MESSAGE_DIGEST_ATTRIB_MISMATCH,
                                         "Authenticated message-digest attribute mismatch: cbHash=%#zx cbValue=%#x",
                                         cbHash, pAttrib->uValues.pOctetStrings->paItems[0].Asn1Core.cb);
                return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_MESSAGE_DIGEST_ATTRIB_MISMATCH,
                                     "Authenticated message-digest attribute mismatch (cbHash=%#zx):\n"
                                     "signed: %.*Rhxs\n"
                                     "our:    %.*Rhxs\n",
                                     cbHash,
                                     cbHash, pAttrib->uValues.pOctetStrings->paItems[0].Asn1Core.uData.pv,
                                     cbHash, RTCrDigestGetHash(*phDigest));
            }
            cMessageDigests++;
        }
    }

    AssertReturn(cContentTypes == 1, VERR_CR_PKCS7_INTERNAL_ERROR);
    AssertReturn(cMessageDigests == 1, VERR_CR_PKCS7_INTERNAL_ERROR);

    /*
     * Calculate the digest of the the authenticated attributes for use in the
     * signature validation.
     */
    if (   pSignerInfo->DigestAlgorithm.Parameters.enmType != RTASN1TYPE_NULL
        && pSignerInfo->DigestAlgorithm.Parameters.enmType != RTASN1TYPE_NOT_PRESENT)
        return RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_DIGEST_PARAMS_NOT_IMPL, "Digest algorithm has unsupported parameters");

    RTCRDIGEST hDigest;
    int rc = RTCrDigestCreateByObjId(&hDigest, &pSignerInfo->DigestAlgorithm.Algorithm);
    if (RT_SUCCESS(rc))
    {
        RTCrDigestRelease(*phDigest);
        *phDigest = hDigest;

        /* ASSUMES that the attributes are encoded according to DER. */
        uint8_t const  *pbData = (uint8_t const *)RTASN1CORE_GET_RAW_ASN1_PTR(&pSignerInfo->AuthenticatedAttributes.SetCore.Asn1Core);
        uint32_t        cbData = RTASN1CORE_GET_RAW_ASN1_SIZE(&pSignerInfo->AuthenticatedAttributes.SetCore.Asn1Core);
        uint8_t         bSetOfTag = ASN1_TAG_SET | ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_CONSTRUCTED;
        rc = RTCrDigestUpdate(hDigest, &bSetOfTag, sizeof(bSetOfTag)); /* Replace the implict tag with a SET-OF tag. */
        if (RT_SUCCESS(rc))
            rc = RTCrDigestUpdate(hDigest, pbData + sizeof(bSetOfTag), cbData - sizeof(bSetOfTag)); /* Skip the implicit tag. */
        if (RT_SUCCESS(rc))
            rc = RTCrDigestFinal(hDigest, NULL, 0);
    }
    return rc;
}


/**
 * Verifies one signature on a PKCS \#7 SignedData.
 *
 * @returns IPRT status code.
 * @param   pSignerInfo         The signature.
 * @param   pSignedData         The SignedData.
 * @param   pahDigests          Array of content digests that runs parallel to
 *                              pSignedData->DigestAlgorithms.
 * @param   fFlags              Verficiation flags.
 * @param   hAdditionalCerts    Store containing optional certificates,
 *                              optional.
 * @param   hTrustedCerts       Store containing trusted certificates, required.
 * @param   pValidationTime     The time we're supposed to validate the
 *                              certificates chains at.
 * @param   pfnVerifyCert       Signing certificate verification callback.
 * @param   pvUser              Callback parameter.
 * @param   pErrInfo            Where to store additional error details,
 *                              optional.
 */
static int rtCrPkcs7VerifySignerInfo(PCRTCRPKCS7SIGNERINFO pSignerInfo, PCRTCRPKCS7SIGNEDDATA pSignedData,
                                     PRTCRDIGEST pahDigests, uint32_t fFlags, RTCRSTORE hAdditionalCerts, RTCRSTORE hTrustedCerts,
                                     PCRTTIMESPEC pValidationTime, RTCRPKCS7VERIFYCERTCALLBACK pfnVerifyCert, void *pvUser,
                                     PRTERRINFO pErrInfo)
{
    /*
     * Check for counter signatures with timestamp.  Verify the signature for
     * the current time if not present.
     */
    /** @todo timestamp counter signatures. */

    /*
     * Locate the certificate used for signing.
     */
    PCRTCRCERTCTX           pSignerCertCtx = NULL;
    PCRTCRX509CERTIFICATE   pSignerCert = NULL;
    RTCRSTORE               hSignerCertSrc = hTrustedCerts;
    if (hSignerCertSrc != NIL_RTCRSTORE)
        pSignerCertCtx = RTCrStoreCertByIssuerAndSerialNo(hSignerCertSrc, &pSignerInfo->IssuerAndSerialNumber.Name,
                                                          &pSignerInfo->IssuerAndSerialNumber.SerialNumber);
    if (!pSignerCertCtx)
    {
        hSignerCertSrc = hAdditionalCerts;
        if (hSignerCertSrc != NIL_RTCRSTORE)
            pSignerCertCtx = RTCrStoreCertByIssuerAndSerialNo(hSignerCertSrc, &pSignerInfo->IssuerAndSerialNumber.Name,
                                                              &pSignerInfo->IssuerAndSerialNumber.SerialNumber);
    }
    if (pSignerCertCtx)
        pSignerCert = pSignerCertCtx->pCert;
    else
    {
        hSignerCertSrc = NULL;
        pSignerCert = RTCrX509Certificates_FindByIssuerAndSerialNumber(&pSignedData->Certificates,
                                                                       &pSignerInfo->IssuerAndSerialNumber.Name,
                                                                       &pSignerInfo->IssuerAndSerialNumber.SerialNumber);
        if (!pSignerCert)
            return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_SIGNED_DATA_CERT_NOT_FOUND,
                                 "Certificate not found: serial=%.*Rhxs",
                                 pSignerInfo->IssuerAndSerialNumber.SerialNumber.Asn1Core.cb,
                                 pSignerInfo->IssuerAndSerialNumber.SerialNumber.Asn1Core.uData.pv);
    }

    /*
     * If not a trusted certificate, we'll have to build certificate paths
     * and verify them.  If no valid paths are found, this step will fail.
     */
    int rc = VINF_SUCCESS;
    if (   hSignerCertSrc == NIL_RTCRSTORE
        || hSignerCertSrc != hTrustedCerts)
    {
        RTCRX509CERTPATHS hCertPaths;
        rc = RTCrX509CertPathsCreate(&hCertPaths, pSignerCert);
        if (RT_SUCCESS(rc))
        {
            rc = RTCrX509CertPathsSetValidTimeSpec(hCertPaths, pValidationTime);
            if (hTrustedCerts != NIL_RTCRSTORE && RT_SUCCESS(rc))
                rc = RTCrX509CertPathsSetTrustedStore(hCertPaths, hTrustedCerts);
            if (hAdditionalCerts != NIL_RTCRSTORE && RT_SUCCESS(rc))
                rc = RTCrX509CertPathsSetUntrustedStore(hCertPaths, hAdditionalCerts);
            if (pSignedData->Certificates.cItems > 0 && RT_SUCCESS(rc))
                rc = RTCrX509CertPathsSetUntrustedArray(hCertPaths,
                                                        pSignedData->Certificates.paItems,
                                                        pSignedData->Certificates.cItems);
            if (RT_SUCCESS(rc))
            {
                rc = RTCrX509CertPathsBuild(hCertPaths, pErrInfo);
                if (RT_SUCCESS(rc))
                    rc = RTCrX509CertPathsValidateAll(hCertPaths, NULL, pErrInfo);

                /*
                 * Check that the certificate purpose and whatnot matches what
                 * is being signed.
                 */
                if (RT_SUCCESS(rc))
                    rc = pfnVerifyCert(pSignerCert, hCertPaths, pvUser, pErrInfo);
            }
            else
                RTErrInfoSetF(pErrInfo, rc, "Error configuring path builder: %Rrc", rc);
            RTCrX509CertPathsRelease(hCertPaths);
        }
    }
    /*
     * Check that the certificate purpose matches what is signed.
     */
    else
        rc = pfnVerifyCert(pSignerCert, NIL_RTCRX509CERTPATHS, pvUser, pErrInfo);

    /*
     * Find the digest that is signed and reference it so we can replace it
     * below if necessary.
     */
    RTCRDIGEST hDigest = NIL_RTCRDIGEST;
    uint32_t iDigest = pSignedData->DigestAlgorithms.cItems;
    while (iDigest-- > 0)
        if (RTCrX509AlgorithmIdentifier_Compare(&pSignedData->DigestAlgorithms.paItems[iDigest],
                                                &pSignerInfo->DigestAlgorithm) == 0)
        {
            hDigest = pahDigests[iDigest];
            uint32_t cRefs = RTCrDigestRetain(hDigest);
            AssertStmt(cRefs != UINT32_MAX, cRefs = NIL_RTCRDIGEST; rc = VERR_CR_PKCS7_INTERNAL_ERROR);
            break;
        }
    if (hDigest == NIL_RTCRDIGEST && RT_SUCCESS(rc))
        rc = RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_DIGEST_ALGO_NOT_FOUND_IN_LIST,
                           "SignerInfo.DigestAlgorithm %s not found.",
                           pSignerInfo->DigestAlgorithm.Algorithm.szObjId);

    /*
     * If there are authenticated attributes, we've got more work before we
     * can verify the signature.
     */
    if (   RT_SUCCESS(rc)
        && RTCrPkcs7Attributes_IsPresent(&pSignerInfo->AuthenticatedAttributes))
        rc = rtCrPkcs7VerifySignerInfoAuthAttribs(pSignerInfo, pSignedData, &hDigest, fFlags, pErrInfo);

    /*
     * Verify the signature.
     */
    if (RT_SUCCESS(rc))
    {
        RTCRPKIXSIGNATURE hSignature;
        rc = RTCrPkixSignatureCreateByObjId(&hSignature,
                                            &pSignerCert->TbsCertificate.SubjectPublicKeyInfo.Algorithm.Algorithm,
                                            false /*fSigning*/,
                                            &pSignerCert->TbsCertificate.SubjectPublicKeyInfo.SubjectPublicKey,
                                            &pSignerInfo->DigestEncryptionAlgorithm.Parameters);
        if (RT_SUCCESS(rc))
        {
            /** @todo Check that DigestEncryptionAlgorithm is compatible with hSignature
             *        (this is not vital). */
            rc = RTCrPkixSignatureVerifyOctetString(hSignature, hDigest, &pSignerInfo->EncryptedDigest);
            if (RT_FAILURE(rc))
                rc = RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_SIGNATURE_VERIFICATION_FAILED,
                                   "Signature verficiation failed: %Rrc", rc);
            RTCrPkixSignatureRelease(hSignature);
        }
        else
            rc = RTErrInfoSetF(pErrInfo, rc, "Failure to instantiate public key algorithm [IPRT]: %s (%s)",
                               pSignerCert->TbsCertificate.SubjectPublicKeyInfo.Algorithm.Algorithm.szObjId,
                               pSignerInfo->DigestEncryptionAlgorithm.Algorithm.szObjId);
    }

    RTCrDigestRelease(hDigest);
    RTCrCertCtxRelease(pSignerCertCtx);
    return rc;
}


RTDECL(int) RTCrPkcs7VerifySignedData(PCRTCRPKCS7CONTENTINFO pContentInfo, uint32_t fFlags,
                                      RTCRSTORE hAdditionalCerts, RTCRSTORE hTrustedCerts,
                                      PCRTTIMESPEC pValidationTime, PRTCRPKCS7VERIFYCERTCALLBACK pfnVerifyCert, void *pvUser,
                                      PRTERRINFO pErrInfo)
{
    /*
     * Check the input.
     */
    if (pfnVerifyCert)
        AssertPtrReturn(pfnVerifyCert, VERR_INVALID_POINTER);
    else
        pfnVerifyCert = RTCrPkcs7VerifyCertCallbackDefault;

    if (!RTCrPkcs7ContentInfo_IsSignedData(pContentInfo))
        return RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_NOT_SIGNED_DATA, "Not PKCS #7 SignedData.");
    PCRTCRPKCS7SIGNEDDATA pSignedData = pContentInfo->u.pSignedData;
    int rc = RTCrPkcs7SignedData_CheckSanity(pSignedData, 0, pErrInfo, "");
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Hash the content info.
     */
    /* Exactly what the content is, for some stupid reason unnecessarily
       complicated.  Figure it out here as we'll need it for the OpenSSL code
       path as well. */
    void const *pvContent = pSignedData->ContentInfo.Content.Asn1Core.uData.pv;
    uint32_t    cbContent = pSignedData->ContentInfo.Content.Asn1Core.cb;
    if (pSignedData->ContentInfo.Content.pEncapsulated)
    {
        pvContent = pSignedData->ContentInfo.Content.pEncapsulated->uData.pv;
        cbContent = pSignedData->ContentInfo.Content.pEncapsulated->cb;
    }

    /* Check that there aren't too many or too few hash algorithms for our
       implementation and purposes. */
    RTCRDIGEST     ahDigests[2];
    uint32_t const cDigests = pSignedData->DigestAlgorithms.cItems;
    if (!cDigests) /** @todo we might have to support this... */
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_NO_DIGEST_ALGORITHMS, "No digest algorithms");

    if (cDigests > RT_ELEMENTS(ahDigests))
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_TOO_MANY_DIGEST_ALGORITHMS,
                             "Too many digest algorithm: cAlgorithms=%u", cDigests);

    /* Create the message digest calculators. */
    rc = VERR_CR_PKCS7_NO_DIGEST_ALGORITHMS;
    uint32_t i;
    for (i = 0; i < cDigests; i++)
    {
        rc = RTCrDigestCreateByObjId(&ahDigests[i], &pSignedData->DigestAlgorithms.paItems[i].Algorithm);
        if (RT_FAILURE(rc))
        {
            rc = RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_DIGEST_CREATE_ERROR, "Error creating digest for '%s': %Rrc",
                               pSignedData->DigestAlgorithms.paItems[i].Algorithm.szObjId, rc);
            break;
        }
    }
    if (RT_SUCCESS(rc))
    {
        /* Hash the content. */
        for (i = 0; i < cDigests && RT_SUCCESS(rc); i++)
        {
            rc = RTCrDigestUpdate(ahDigests[i], pvContent, cbContent);
            if (RT_SUCCESS(rc))
                rc = RTCrDigestFinal(ahDigests[i], NULL, 0);
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Validate the signed infos.
             */
            rc = VERR_CR_PKCS7_NO_SIGNER_INFOS;
            for (i = 0; i < pSignedData->SignerInfos.cItems; i++)
            {
                rc = rtCrPkcs7VerifySignerInfo(&pSignedData->SignerInfos.paItems[i], pSignedData, ahDigests,
                                               fFlags, hAdditionalCerts, hTrustedCerts, pValidationTime,
                                               pfnVerifyCert, pvUser, pErrInfo);
                if (RT_FAILURE(rc))
                    break;
            }
        }
        else
            rc = RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_DIGEST_CALC_ERROR,
                               "Hashing content failed unexpectedly (i=%u): %Rrc", i, rc);

        /* Clean up digests. */
        i = cDigests;
    }
    while (i-- > 0)
    {
        int rc2 = RTCrDigestRelease(ahDigests[i]);
        AssertRC(rc2);
    }


#ifdef IPRT_WITH_OPENSSL
    /*
     * Verify using OpenSSL and combine the results (should be identical).
     */
    int rcOssl = rtCrPkcs7VerifySignedDataUsingOpenSsl(pContentInfo, fFlags, hAdditionalCerts, hTrustedCerts,
                                                       pvContent, cbContent, RT_SUCCESS(rc) ? pErrInfo : NULL);
    if (RT_SUCCESS(rcOssl) && RT_SUCCESS(rc))
        return rc;
//    AssertMsg(RT_FAILURE_NP(rcOssl) && RT_FAILURE_NP(rc), ("%Rrc, %Rrc\n", rcOssl, rc));
    if (RT_FAILURE(rc))
        return rc;
    return rcOssl;
#else
    return rc;
#endif
}

