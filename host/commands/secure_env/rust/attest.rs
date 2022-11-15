//! Attestation keys and certificates.
//!
//! Hard-coded keys and certs copied from system/keymaster/context/soft_attestation_cert.cpp

use kmr_common::{
    crypto::ec, crypto::rsa, crypto::CurveType, crypto::KeyMaterial, wire::keymint,
    wire::keymint::EcCurve, Error,
};
use kmr_ta::device::{RetrieveCertSigningInfo, SigningAlgorithm, SigningKeyType};

/// RSA attestation private key in PKCS#1 format.
///
/// Decoded contents (using [der2ascii](https://github.com/google/der-ascii)):
///
/// ```
/// SEQUENCE {
///   INTEGER { 0 }
///   INTEGER { `00c08323dc56881bb8302069f5b08561c6eebe7f05e2f5a842048abe8b47be76feaef25cf29b2afa3200141601429989a15fcfc6815eb363583c2fd2f20be4983283dd814b16d7e185417ae54abc296a3a6db5c004083b68c556c1f02339916419864d50b74d40aeca484c77356c895a0c275abfac499d5d7d2362f29c5e02e871` }
///   INTEGER { 65537 }
///   INTEGER { `00be860b0b99a802a6fb1a59438a7bb715065b09a36dc6e9cacc6bf3c02c34d7d79e94c6606428d88c7b7f6577c1cdea64074abe8e7286df1f0811dc9728260868de95d32efc96b6d084ff271a5f60defcc703e7a38e6e29ba9a3c5fc2c28076b6a896af1d34d78828ce9bddb1f34f9c9404430781298e201316725bbdbc993a41` }
///   INTEGER { `00e1c6d927646c0916ec36826d594983740c21f1b074c4a1a59867c669795c85d3dc464c5b929e94bfb34e0dcc5014b10f13341ab7fdd5f60414d2a326cad41cc5` }
///   INTEGER { `00da485997785cd5630fb0fd8c5254f98e538e18983aae9e6b7e6a5a7b5d343755b9218ebd40320d28387d789f76fa218bcc2d8b68a5f6418fbbeca5179ab3afbd` }
///   INTEGER { `50fefc32649559616ed6534e154509329d93a3d810dbe5bdb982292cf78bd8badb8020ae8d57f4b71d05386ffe9e9db271ca3477a34999db76f8e5ece9c0d49d` }
///   INTEGER { `15b74cf27cceff8bb36bf04d9d8346b09a2f70d2f4439b0f26ac7e03f7e9d1f77d4b915fd29b2823f03acb5d5200e0857ff2a803e93eee96d6235ce95442bc21` }
///   INTEGER { `0090a745da8970b2cd649660324228c5f82856ffd665ba9a85c8d60f1b8bee717ecd2c72eae01dad86ba7654d4cf45adb5f1f2b31d9f8122cfa5f1a5570f9b2d25` }
/// }
/// ```
const RSA_ATTEST_KEY: &str = concat!(
    "3082025d02010002818100c08323dc56881bb8302069f5b08561c6eebe7f05e2",
    "f5a842048abe8b47be76feaef25cf29b2afa3200141601429989a15fcfc6815e",
    "b363583c2fd2f20be4983283dd814b16d7e185417ae54abc296a3a6db5c00408",
    "3b68c556c1f02339916419864d50b74d40aeca484c77356c895a0c275abfac49",
    "9d5d7d2362f29c5e02e871020301000102818100be860b0b99a802a6fb1a5943",
    "8a7bb715065b09a36dc6e9cacc6bf3c02c34d7d79e94c6606428d88c7b7f6577",
    "c1cdea64074abe8e7286df1f0811dc9728260868de95d32efc96b6d084ff271a",
    "5f60defcc703e7a38e6e29ba9a3c5fc2c28076b6a896af1d34d78828ce9bddb1",
    "f34f9c9404430781298e201316725bbdbc993a41024100e1c6d927646c0916ec",
    "36826d594983740c21f1b074c4a1a59867c669795c85d3dc464c5b929e94bfb3",
    "4e0dcc5014b10f13341ab7fdd5f60414d2a326cad41cc5024100da485997785c",
    "d5630fb0fd8c5254f98e538e18983aae9e6b7e6a5a7b5d343755b9218ebd4032",
    "0d28387d789f76fa218bcc2d8b68a5f6418fbbeca5179ab3afbd024050fefc32",
    "649559616ed6534e154509329d93a3d810dbe5bdb982292cf78bd8badb8020ae",
    "8d57f4b71d05386ffe9e9db271ca3477a34999db76f8e5ece9c0d49d024015b7",
    "4cf27cceff8bb36bf04d9d8346b09a2f70d2f4439b0f26ac7e03f7e9d1f77d4b",
    "915fd29b2823f03acb5d5200e0857ff2a803e93eee96d6235ce95442bc210241",
    "0090a745da8970b2cd649660324228c5f82856ffd665ba9a85c8d60f1b8bee71",
    "7ecd2c72eae01dad86ba7654d4cf45adb5f1f2b31d9f8122cfa5f1a5570f9b2d",
    "25",
);

/// Attestation certificate corresponding to [`RSA_ATTEST_KEY`], signed by the key in
/// [`RSA_ATTEST_ROOT_CERT`].
///
/// Decoded contents:
///
/// ```
/// Certificate:
///     Data:
///         Version: 3 (0x2)
///         Serial Number: 4096 (0x1000)
///     Signature Algorithm: SHA256-RSA
///         Issuer: C=US, O=Google, Inc., OU=Android, L=Mountain View, ST=California
///         Validity:
///             Not Before: 2016-01-04 12:40:53 +0000 UTC
///             Not After : 2035-12-30 12:40:53 +0000 UTC
///         Subject: C=US, O=Google, Inc., OU=Android, ST=California, CN=Android Software Attestation Key
///         Subject Public Key Info:
///             Public Key Algorithm: rsaEncryption
///                 Public Key: (1024 bit)
///                 Modulus:
///                     c0:83:23:dc:56:88:1b:b8:30:20:69:f5:b0:85:61:
///                     c6:ee:be:7f:05:e2:f5:a8:42:04:8a:be:8b:47:be:
///                     76:fe:ae:f2:5c:f2:9b:2a:fa:32:00:14:16:01:42:
///                     99:89:a1:5f:cf:c6:81:5e:b3:63:58:3c:2f:d2:f2:
///                     0b:e4:98:32:83:dd:81:4b:16:d7:e1:85:41:7a:e5:
///                     4a:bc:29:6a:3a:6d:b5:c0:04:08:3b:68:c5:56:c1:
///                     f0:23:39:91:64:19:86:4d:50:b7:4d:40:ae:ca:48:
///                     4c:77:35:6c:89:5a:0c:27:5a:bf:ac:49:9d:5d:7d:
///                     23:62:f2:9c:5e:02:e8:71:
///                 Exponent: 65537 (0x10001)
///         X509v3 extensions:
///             X509v3 Authority Key Identifier:
///                 keyid:29faf1accc4dd24c96402775b6b0e932e507fe2e
///             X509v3 Subject Key Identifier:
///                 keyid:d40c101bf8cd63b9f73952b50e135ca6d7999386
///             X509v3 Key Usage: critical
///                 Digital Signature, Certificate Signing
///             X509v3 Basic Constraints: critical
///                 CA:true, pathlen:0
///     Signature Algorithm: SHA256-RSA
///          9e:2d:48:5f:8c:67:33:dc:1a:85:ad:99:d7:50:23:ea:14:ec:
///          43:b0:e1:9d:ea:c2:23:46:1e:72:b5:19:dc:60:22:e4:a5:68:
///          31:6c:0b:55:c4:e6:9c:a2:2d:9f:3a:4f:93:6b:31:8b:16:78:
///          16:0d:88:cb:d9:8b:cc:80:9d:84:f0:c2:27:e3:6b:38:f1:fd:
///          d1:e7:17:72:31:59:35:7d:96:f3:c5:7f:ab:9d:8f:96:61:26:
///          4f:b2:be:81:bb:0d:49:04:22:8a:ce:9f:f7:f5:42:2e:25:44:
///          fa:21:07:12:5a:83:b5:55:ad:18:82:f8:40:14:9b:9c:20:63:
///          04:7f:
/// ```
const RSA_ATTEST_CERT: &str = concat!(
    "308202b63082021fa00302010202021000300d06092a864886f70d01010b0500",
    "3063310b30090603550406130255533113301106035504080c0a43616c69666f",
    "726e69613116301406035504070c0d4d6f756e7461696e205669657731153013",
    "060355040a0c0c476f6f676c652c20496e632e3110300e060355040b0c07416e",
    "64726f6964301e170d3136303130343132343035335a170d3335313233303132",
    "343035335a3076310b30090603550406130255533113301106035504080c0a43",
    "616c69666f726e696131153013060355040a0c0c476f6f676c652c20496e632e",
    "3110300e060355040b0c07416e64726f69643129302706035504030c20416e64",
    "726f696420536f667477617265204174746573746174696f6e204b657930819f",
    "300d06092a864886f70d010101050003818d0030818902818100c08323dc5688",
    "1bb8302069f5b08561c6eebe7f05e2f5a842048abe8b47be76feaef25cf29b2a",
    "fa3200141601429989a15fcfc6815eb363583c2fd2f20be4983283dd814b16d7",
    "e185417ae54abc296a3a6db5c004083b68c556c1f02339916419864d50b74d40",
    "aeca484c77356c895a0c275abfac499d5d7d2362f29c5e02e8710203010001a3",
    "663064301d0603551d0e04160414d40c101bf8cd63b9f73952b50e135ca6d799",
    "9386301f0603551d2304183016801429faf1accc4dd24c96402775b6b0e932e5",
    "07fe2e30120603551d130101ff040830060101ff020100300e0603551d0f0101",
    "ff040403020284300d06092a864886f70d01010b0500038181009e2d485f8c67",
    "33dc1a85ad99d75023ea14ec43b0e19deac223461e72b519dc6022e4a568316c",
    "0b55c4e69ca22d9f3a4f936b318b1678160d88cbd98bcc809d84f0c227e36b38",
    "f1fdd1e717723159357d96f3c57fab9d8f9661264fb2be81bb0d4904228ace9f",
    "f7f5422e2544fa2107125a83b555ad1882f840149b9c2063047f",
);

/// Attestation self-signed root certificate holding the key that signed [`RSA_ATTEST_CERT`].
///
/// Decoded contents:
///
/// ```
/// Certificate:
///     Data:
///         Version: 3 (0x2)
///         Serial Number: 18416584322103887884 (0xff94d9dd9f07c80c)
///     Signature Algorithm: SHA256-RSA
///         Issuer: C=US, O=Google, Inc., OU=Android, L=Mountain View, ST=California
///         Validity:
///             Not Before: 2016-01-04 12:31:08 +0000 UTC
///             Not After : 2035-12-30 12:31:08 +0000 UTC
///         Subject: C=US, O=Google, Inc., OU=Android, L=Mountain View, ST=California
///         Subject Public Key Info:
///             Public Key Algorithm: rsaEncryption
///                 Public Key: (1024 bit)
///                 Modulus:
///                     a2:6b:ad:eb:6e:2e:44:61:ef:d5:0e:82:e6:b7:94:
///                     d1:75:23:1f:77:9b:63:91:63:ff:f7:aa:ff:0b:72:
///                     47:4e:c0:2c:43:ec:33:7c:d7:ac:ed:40:3e:8c:28:
///                     a0:66:d5:f7:87:0b:33:97:de:0e:b8:4e:13:40:ab:
///                     af:a5:27:bf:95:69:a0:31:db:06:52:65:f8:44:59:
///                     57:61:f0:bb:f2:17:4b:b7:41:80:64:c0:28:0e:8f:
///                     52:77:8e:db:d2:47:b6:45:e9:19:c8:e9:8b:c3:db:
///                     c2:91:3f:d7:d7:50:c4:1d:35:66:f9:57:e4:97:96:
///                     0b:09:ac:ce:92:35:85:9b:
///                 Exponent: 65537 (0x10001)
///         X509v3 extensions:
///             X509v3 Authority Key Identifier:
///                 keyid:29faf1accc4dd24c96402775b6b0e932e507fe2e
///             X509v3 Subject Key Identifier:
///                 keyid:29faf1accc4dd24c96402775b6b0e932e507fe2e
///             X509v3 Key Usage: critical
///                 Digital Signature, Certificate Signing
///             X509v3 Basic Constraints: critical
///                 CA:true
///     Signature Algorithm: SHA256-RSA
///          4f:72:f3:36:59:8d:0e:c1:b9:74:5b:31:59:f6:f0:8d:25:49:
///          30:9e:a3:1c:1c:29:d2:45:2d:20:b9:4d:5f:64:b4:e8:80:c7:
///          78:7a:9c:39:de:a8:b3:f5:bf:2f:70:5f:47:10:5c:c5:e6:eb:
///          4d:06:99:61:d2:ae:9a:07:ff:f7:7c:b8:ab:eb:9c:0f:24:07:
///          5e:b1:7f:ba:79:71:fd:4d:5b:9e:df:14:a9:fe:df:ed:7c:c0:
///          88:5d:f8:dd:9b:64:32:56:d5:35:9a:e2:13:f9:8f:ce:c1:7c:
///          dc:ef:a4:aa:b2:55:c3:83:a9:2e:fb:5c:f6:62:f5:27:52:17:
///          be:63:
/// ```
const RSA_ATTEST_ROOT_CERT: &str = concat!(
    "308202a730820210a003020102020900ff94d9dd9f07c80c300d06092a864886",
    "f70d01010b05003063310b30090603550406130255533113301106035504080c",
    "0a43616c69666f726e69613116301406035504070c0d4d6f756e7461696e2056",
    "69657731153013060355040a0c0c476f6f676c652c20496e632e3110300e0603",
    "55040b0c07416e64726f6964301e170d3136303130343132333130385a170d33",
    "35313233303132333130385a3063310b30090603550406130255533113301106",
    "035504080c0a43616c69666f726e69613116301406035504070c0d4d6f756e74",
    "61696e205669657731153013060355040a0c0c476f6f676c652c20496e632e31",
    "10300e060355040b0c07416e64726f696430819f300d06092a864886f70d0101",
    "01050003818d0030818902818100a26badeb6e2e4461efd50e82e6b794d17523",
    "1f779b639163fff7aaff0b72474ec02c43ec337cd7aced403e8c28a066d5f787",
    "0b3397de0eb84e1340abafa527bf9569a031db065265f844595761f0bbf2174b",
    "b7418064c0280e8f52778edbd247b645e919c8e98bc3dbc2913fd7d750c41d35",
    "66f957e497960b09acce9235859b0203010001a3633061301d0603551d0e0416",
    "041429faf1accc4dd24c96402775b6b0e932e507fe2e301f0603551d23041830",
    "16801429faf1accc4dd24c96402775b6b0e932e507fe2e300f0603551d130101",
    "ff040530030101ff300e0603551d0f0101ff040403020284300d06092a864886",
    "f70d01010b0500038181004f72f336598d0ec1b9745b3159f6f08d2549309ea3",
    "1c1c29d2452d20b94d5f64b4e880c7787a9c39dea8b3f5bf2f705f47105cc5e6",
    "eb4d069961d2ae9a07fff77cb8abeb9c0f24075eb17fba7971fd4d5b9edf14a9",
    "fedfed7cc0885df8dd9b643256d5359ae213f98fcec17cdcefa4aab255c383a9",
    "2efb5cf662f5275217be63",
);

/// EC attestation private key in `ECPrivateKey` format.
///
/// Decoded contents (using [der2ascii](https://github.com/google/der-ascii)):
///
/// ```
/// SEQUENCE {
///   INTEGER { 1 }
///   OCTET_STRING { `21e086432a15198459cf363a50fc14c9daadf935f527c2dfd71e4d6dbc42e544` }
///   [0] {
///     # secp256r1
///     OBJECT_IDENTIFIER { 1.2.840.10045.3.1.7 }
///   }
///   [1] {
///     BIT_STRING { `00` `04eb9e79f8426359accb2a914c8986cc70ad90669382a9732613feaccbf821274c2174974a2afea5b94d7f66d4e065106635bc53b7a0a3a671583edb3e11ae1014` }
///   }
/// }
/// ```
const EC_ATTEST_KEY: &str = concat!(
    "3077020101042021e086432a15198459cf363a50fc14c9daadf935f527c2dfd7",
    "1e4d6dbc42e544a00a06082a8648ce3d030107a14403420004eb9e79f8426359",
    "accb2a914c8986cc70ad90669382a9732613feaccbf821274c2174974a2afea5",
    "b94d7f66d4e065106635bc53b7a0a3a671583edb3e11ae1014",
);

/// Attestation certificate corresponding to [`EC_ATTEST_KEY`], signed by the key in
/// [`EC_ATTEST_ROOT_CERT`].
///
/// Decoded contents:
///
/// ```
/// Certificate:
///     Data:
///         Version: 3 (0x2)
///         Serial Number: 4097 (0x1001)
///     Signature Algorithm: ECDSA-SHA256
///         Issuer: C=US, O=Google, Inc., OU=Android, L=Mountain View, ST=California, CN=Android Keystore Software Attestation Root
///         Validity:
///             Not Before: 2016-01-11 00:46:09 +0000 UTC
///             Not After : 2026-01-08 00:46:09 +0000 UTC
///         Subject: C=US, O=Google, Inc., OU=Android, ST=California, CN=Android Keystore Software Attestation Intermediate
///         Subject Public Key Info:
///             Public Key Algorithm: id-ecPublicKey
///                 Public Key: (256 bit)
///                 pub:
///                     04:eb:9e:79:f8:42:63:59:ac:cb:2a:91:4c:89:86:
///                     cc:70:ad:90:66:93:82:a9:73:26:13:fe:ac:cb:f8:
///                     21:27:4c:21:74:97:4a:2a:fe:a5:b9:4d:7f:66:d4:
///                     e0:65:10:66:35:bc:53:b7:a0:a3:a6:71:58:3e:db:
///                     3e:11:ae:10:14:
///                 ASN1 OID: prime256v1
///         X509v3 extensions:
///             X509v3 Authority Key Identifier:
///                 keyid:c8ade9774c45c3a3cf0d1610e479433a215a30cf
///             X509v3 Subject Key Identifier:
///                 keyid:3ffcacd61ab13a9e8120b8d5251cc565bb1e91a9
///             X509v3 Key Usage: critical
///                 Digital Signature, Certificate Signing
///             X509v3 Basic Constraints: critical
///                 CA:true, pathlen:0
///     Signature Algorithm: ECDSA-SHA256
///          30:45:02:20:4b:8a:9b:7b:ee:82:bc:c0:33:87:ae:2f:c0:89:
///          98:b4:dd:c3:8d:ab:27:2a:45:9f:69:0c:c7:c3:92:d4:0f:8e:
///          02:21:00:ee:da:01:5d:b6:f4:32:e9:d4:84:3b:62:4c:94:04:
///          ef:3a:7c:cc:bd:5e:fb:22:bb:e7:fe:b9:77:3f:59:3f:fb:
/// ```
const EC_ATTEST_CERT: &str = concat!(
    "308202783082021ea00302010202021001300a06082a8648ce3d040302308198",
    "310b30090603550406130255533113301106035504080c0a43616c69666f726e",
    "69613116301406035504070c0d4d6f756e7461696e2056696577311530130603",
    "55040a0c0c476f6f676c652c20496e632e3110300e060355040b0c07416e6472",
    "6f69643133303106035504030c2a416e64726f6964204b657973746f72652053",
    "6f667477617265204174746573746174696f6e20526f6f74301e170d31363031",
    "31313030343630395a170d3236303130383030343630395a308188310b300906",
    "03550406130255533113301106035504080c0a43616c69666f726e6961311530",
    "13060355040a0c0c476f6f676c652c20496e632e3110300e060355040b0c0741",
    "6e64726f6964313b303906035504030c32416e64726f6964204b657973746f72",
    "6520536f667477617265204174746573746174696f6e20496e7465726d656469",
    "6174653059301306072a8648ce3d020106082a8648ce3d03010703420004eb9e",
    "79f8426359accb2a914c8986cc70ad90669382a9732613feaccbf821274c2174",
    "974a2afea5b94d7f66d4e065106635bc53b7a0a3a671583edb3e11ae1014a366",
    "3064301d0603551d0e041604143ffcacd61ab13a9e8120b8d5251cc565bb1e91",
    "a9301f0603551d23041830168014c8ade9774c45c3a3cf0d1610e479433a215a",
    "30cf30120603551d130101ff040830060101ff020100300e0603551d0f0101ff",
    "040403020284300a06082a8648ce3d040302034800304502204b8a9b7bee82bc",
    "c03387ae2fc08998b4ddc38dab272a459f690cc7c392d40f8e022100eeda015d",
    "b6f432e9d4843b624c9404ef3a7cccbd5efb22bbe7feb9773f593ffb",
);

/// Attestation self-signed root certificate holding the key that signed [`EC_ATTEST_CERT`].
///
/// Decoded contents:
///
/// ```
/// Certificate:
///     Data:
///         Version: 3 (0x2)
///         Serial Number: 11674912229752527703 (0xa2059ed10e435b57)
///     Signature Algorithm: ECDSA-SHA256
///         Issuer: C=US, O=Google, Inc., OU=Android, L=Mountain View, ST=California, CN=Android Keystore Software Attestation Root
///         Validity:
///             Not Before: 2016-01-11 00:43:50 +0000 UTC
///             Not After : 2036-01-06 00:43:50 +0000 UTC
///         Subject: C=US, O=Google, Inc., OU=Android, L=Mountain View, ST=California, CN=Android Keystore Software Attestation Root
///         Subject Public Key Info:
///             Public Key Algorithm: id-ecPublicKey
///                 Public Key: (256 bit)
///                 pub:
///                     04:ee:5d:5e:c7:e1:c0:db:6d:03:a6:7e:e6:b6:1b:
///                     ec:4d:6a:5d:6a:68:2e:0f:ff:7f:49:0e:7d:77:1f:
///                     44:22:6d:bd:b1:af:fa:16:cb:c7:ad:c5:77:d2:56:
///                     9c:aa:b7:b0:2d:54:01:5d:3e:43:2b:2a:8e:d7:4e:
///                     ec:48:75:41:a4:
///                 ASN1 OID: prime256v1
///         X509v3 extensions:
///             X509v3 Authority Key Identifier:
///                 keyid:c8ade9774c45c3a3cf0d1610e479433a215a30cf
///             X509v3 Subject Key Identifier:
///                 keyid:c8ade9774c45c3a3cf0d1610e479433a215a30cf
///             X509v3 Key Usage: critical
///                 Digital Signature, Certificate Signing
///             X509v3 Basic Constraints: critical
///                 CA:true
///     Signature Algorithm: ECDSA-SHA256
///          30:44:02:20:35:21:a3:ef:8b:34:46:1e:9c:d5:60:f3:1d:58:
///          89:20:6a:dc:a3:65:41:f6:0d:9e:ce:8a:19:8c:66:48:60:7b:
///          02:20:4d:0b:f3:51:d9:30:7c:7d:5b:da:35:34:1d:a8:47:1b:
///          63:a5:85:65:3c:ad:4f:24:a7:e7:4d:af:41:7d:f1:bf:
/// ```
const EC_ATTEST_ROOT_CERT: &str = concat!(
    "3082028b30820232a003020102020900a2059ed10e435b57300a06082a8648ce",
    "3d040302308198310b30090603550406130255533113301106035504080c0a43",
    "616c69666f726e69613116301406035504070c0d4d6f756e7461696e20566965",
    "7731153013060355040a0c0c476f6f676c652c20496e632e3110300e06035504",
    "0b0c07416e64726f69643133303106035504030c2a416e64726f6964204b6579",
    "73746f726520536f667477617265204174746573746174696f6e20526f6f7430",
    "1e170d3136303131313030343335305a170d3336303130363030343335305a30",
    "8198310b30090603550406130255533113301106035504080c0a43616c69666f",
    "726e69613116301406035504070c0d4d6f756e7461696e205669657731153013",
    "060355040a0c0c476f6f676c652c20496e632e3110300e060355040b0c07416e",
    "64726f69643133303106035504030c2a416e64726f6964204b657973746f7265",
    "20536f667477617265204174746573746174696f6e20526f6f74305930130607",
    "2a8648ce3d020106082a8648ce3d03010703420004ee5d5ec7e1c0db6d03a67e",
    "e6b61bec4d6a5d6a682e0fff7f490e7d771f44226dbdb1affa16cbc7adc577d2",
    "569caab7b02d54015d3e432b2a8ed74eec487541a4a3633061301d0603551d0e",
    "04160414c8ade9774c45c3a3cf0d1610e479433a215a30cf301f0603551d2304",
    "1830168014c8ade9774c45c3a3cf0d1610e479433a215a30cf300f0603551d13",
    "0101ff040530030101ff300e0603551d0f0101ff040403020284300a06082a86",
    "48ce3d040302034700304402203521a3ef8b34461e9cd560f31d5889206adca3",
    "6541f60d9ece8a198c6648607b02204d0bf351d9307c7d5bda35341da8471b63",
    "a585653cad4f24a7e74daf417df1bf",
);

/// Per-algorithm attestation certificate signing information.
pub(crate) struct CertSignAlgoInfo {
    key: KeyMaterial,
    chain: Vec<keymint::Certificate>,
}

pub(crate) struct CertSignInfo {
    rsa_info: CertSignAlgoInfo,
    ec_info: CertSignAlgoInfo,
}

impl CertSignInfo {
    pub(crate) fn new() -> Self {
        CertSignInfo {
            rsa_info: CertSignAlgoInfo {
                key: KeyMaterial::Rsa(rsa::Key(hex::decode(RSA_ATTEST_KEY).unwrap()).into()),
                chain: vec![
                    keymint::Certificate {
                        encoded_certificate: hex::decode(RSA_ATTEST_CERT).unwrap(),
                    },
                    keymint::Certificate {
                        encoded_certificate: hex::decode(RSA_ATTEST_ROOT_CERT).unwrap(),
                    },
                ],
            },
            ec_info: CertSignAlgoInfo {
                key: KeyMaterial::Ec(
                    EcCurve::P256,
                    CurveType::Nist,
                    ec::Key::P256(ec::NistKey(hex::decode(EC_ATTEST_KEY).unwrap())).into(),
                ),
                chain: vec![
                    keymint::Certificate {
                        encoded_certificate: hex::decode(EC_ATTEST_CERT).unwrap(),
                    },
                    keymint::Certificate {
                        encoded_certificate: hex::decode(EC_ATTEST_ROOT_CERT).unwrap(),
                    },
                ],
            },
        }
    }
}

impl RetrieveCertSigningInfo for CertSignInfo {
    fn signing_key(&self, key_type: SigningKeyType) -> Result<KeyMaterial, Error> {
        Ok(match key_type.algo_hint {
            SigningAlgorithm::Rsa => self.rsa_info.key.clone(),
            SigningAlgorithm::Ec => self.ec_info.key.clone(),
        })
    }

    fn cert_chain(&self, key_type: SigningKeyType) -> Result<Vec<keymint::Certificate>, Error> {
        Ok(match key_type.algo_hint {
            SigningAlgorithm::Rsa => self.rsa_info.chain.clone(),
            SigningAlgorithm::Ec => self.ec_info.chain.clone(),
        })
    }
}
