#include "DkimManager.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace containercp::mail {

std::string DkimManager::generate_key(const std::string& dkim_dir,
                                       const std::string& domain,
                                       const std::string& selector) {
    // Create domain DKIM directory
    std::string domain_dir = dkim_dir + "/" + domain;
    std::string mkdir_cmd = "mkdir -p " + domain_dir;
    if (std::system(mkdir_cmd.c_str()) != 0) return "";

    // Generate 2048-bit RSA private key
    std::string priv_path = domain_dir + "/" + selector + ".private";
    std::string gen_cmd = "openssl genrsa -out " + priv_path + " 2048 2>/dev/null";
    if (std::system(gen_cmd.c_str()) != 0) return "";

    // Extract public key in DNS TXT format (base64 DER)
    std::string pub_tmp = "/tmp/containercp-dkim-pub.txt";
    std::string pub_cmd = "openssl rsa -in " + priv_path + " -pubout 2>/dev/null | "
                          "openssl pkey -pubin -outform DER 2>/dev/null | "
                          "openssl base64 -A > " + pub_tmp;
    if (std::system(pub_cmd.c_str()) != 0) return "";

    std::ifstream in(pub_tmp);
    std::string pubkey_b64;
    std::getline(in, pubkey_b64);
    std::remove(pub_tmp.c_str());

    if (pubkey_b64.empty()) return "";

    // Build DNS TXT record value
    return "v=DKIM1; k=rsa; p=" + pubkey_b64;
}

} // namespace containercp::mail
