/**
 *    Copyright (C) 2020-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kControl

#include "mongo/platform/basic.h"

#include "mongo/shell/kms_network.h"

namespace mongo {

void KMSNetworkConnection::connect(const HostAndPort& host) {
    SockAddr server(host.host().c_str(), host.port(), AF_UNSPEC);

    uassert(51136,
            str::stream() << "KMS server address " << host.host() << " is invalid.",
            server.isValid());

    int attempt = 0;
    bool connected = false;
    while (!connected && attempt < 20) {
        connected = _socket->connect(server);
        attempt++;
    }
    uassert(
        51137, str::stream() << "Could not connect to KMS server " << server.toString(), connected);

    uassert(51138,
            str::stream() << "Failed to perform SSL handshake with the KMS server "
                          << host.toString(),
            _socket->secure(_sslManager, host.host()));
}

// Sends a request message to the KMS server and creates a KMS Response.
UniqueKmsResponse KMSNetworkConnection::sendRequest(ConstDataRange request) {
    std::array<char, 512> resp;

    _socket->send(reinterpret_cast<const char*>(request.data()), request.length(), "KMS request");

    auto parser = UniqueKmsResponseParser(kms_response_parser_new());
    int bytes_to_read = 0;

    while ((bytes_to_read = kms_response_parser_wants_bytes(parser.get(), resp.size())) > 0) {
        bytes_to_read = std::min(bytes_to_read, static_cast<int>(resp.size()));
        bytes_to_read = _socket->unsafe_recv(resp.data(), bytes_to_read);

        uassert(51139,
                "kms_response_parser_feed failed",
                kms_response_parser_feed(
                    parser.get(), reinterpret_cast<uint8_t*>(resp.data()), bytes_to_read));
    }

    auto response = UniqueKmsResponse(kms_response_parser_get_response(parser.get()));

    return response;
}

UniqueKmsResponse KMSNetworkConnection::makeOneRequest(const HostAndPort& host,
                                                       ConstDataRange request) {
    connect(host);

    auto resp = sendRequest(request);

    _socket->close();

    return resp;
}

void getSSLParamsForNetworkKMS(SSLParams* params) {
    params->sslPEMKeyFile = "";
    params->sslPEMKeyPassword = "";
    params->sslClusterFile = "";
    params->sslClusterPassword = "";
    params->sslCAFile = "";

    params->sslCRLFile = "";

    // Copy the rest from the global SSL manager options.
    params->sslFIPSMode = sslGlobalParams.sslFIPSMode;

    // KMS servers never should have invalid certificates
    params->sslAllowInvalidCertificates = false;
    params->sslAllowInvalidHostnames = false;

    params->sslDisabledProtocols =
        std::vector({SSLParams::Protocols::TLS1_0, SSLParams::Protocols::TLS1_1});
}


}  // namespace mongo