#include "DkimManager.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unistd.h>

namespace containercp::mail {

DkimManager::DkimManager(logger::Logger& logger)
    : logger_(logger)
{
}

std::string DkimManager::validate_label(const std::string& label,
                                         const std::string& name) {
    if (label.empty()) {
        return name + " must not be empty";
    }
    for (char c : label) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') {
            return name + " contains invalid characters";
        }
    }
    return "";
}

std::string DkimManager::generate_key(const std::string& dkim_dir,
                                       const std::string& domain,
                                       const std::string& selector) {
    // Validate inputs before any filesystem operations
    std::string domain_err = validate_label(domain, "Domain");
    if (!domain_err.empty()) {
        logger_.error("DKIM", domain_err + ": " + domain);
        return "";
    }
    std::string sel_err = validate_label(selector, "Selector");
    if (!sel_err.empty()) {
        logger_.error("DKIM", sel_err + ": " + selector);
        return "";
    }

    // Create domain DKIM directory
    std::string domain_dir = dkim_dir + "/" + domain;
    auto mkdir = executor_.run({"mkdir", "-p", domain_dir});
    if (mkdir.exit_code != 0) {
        logger_.error("DKIM", "Failed to create directory: " + mkdir.err);
        return "";
    }

    // Generate 2048-bit RSA private key via CommandExecutor
    std::string priv_path = domain_dir + "/" + selector + ".private";
    auto gen = executor_.run({
        "openssl", "genrsa", "-out", priv_path, "2048"
    });
    if (gen.exit_code != 0) {
        logger_.error("DKIM", "genrsa failed: " + gen.err);
        return "";
    }

    // Extract public key using three sequential CommandExecutor calls
    // (no shell pipelines, no std::system).

    // Step 1: extract public key in PEM format
    auto pub_pem = executor_.run({
        "openssl", "rsa", "-in", priv_path, "-pubout"
    });
    if (pub_pem.exit_code != 0) {
        logger_.error("DKIM", "Failed to extract public key: " + pub_pem.err);
        return "";
    }

    // Step 2: write PEM to a unique temp file for the next command
    std::string pem_buf = pub_pem.out;
    char pem_tmpl[] = "/tmp/containercp-dkim-pem-XXXXXX";
    int pem_fd = ::mkstemp(pem_tmpl);
    if (pem_fd < 0) {
        logger_.error("DKIM", "Failed to create temp file (pem)");
        return "";
    }
    ::write(pem_fd, pem_buf.data(), pem_buf.size());
    ::close(pem_fd);

    // Step 3: convert PEM to DER format
    char der_tmpl[] = "/tmp/containercp-dkim-der-XXXXXX";
    int der_fd = ::mkstemp(der_tmpl);
    if (der_fd < 0) {
        logger_.error("DKIM", "Failed to create temp file (der)");
        std::remove(pem_tmpl);
        return "";
    }
    ::close(der_fd);

    auto pub_der = executor_.run({
        "openssl", "pkey", "-pubin", "-in", pem_tmpl,
        "-outform", "DER", "-out", der_tmpl
    });
    std::remove(pem_tmpl);
    if (pub_der.exit_code != 0) {
        logger_.error("DKIM", "Failed to convert public key to DER: " + pub_der.err);
        std::remove(der_tmpl);
        return "";
    }

    // Step 4: base64-encode the DER into stdout
    auto b64 = executor_.run({
        "openssl", "base64", "-A", "-in", der_tmpl
    });
    std::remove(der_tmpl);

    if (b64.exit_code != 0) {
        logger_.error("DKIM", "Failed to base64-encode public key: " + b64.err);
        return "";
    }

    std::string pubkey_b64 = b64.out;
    // Trim trailing newline if present
    while (!pubkey_b64.empty() &&
           (pubkey_b64.back() == '\n' || pubkey_b64.back() == '\r')) {
        pubkey_b64.pop_back();
    }

    if (pubkey_b64.empty()) {
        logger_.error("DKIM", "Extracted public key is empty");
        return "";
    }

    logger_.info("DKIM", "Key generated for " + domain
                 + " (selector: " + selector + ")");
    return "v=DKIM1; k=rsa; p=" + pubkey_b64;
}

} // namespace containercp::mail
