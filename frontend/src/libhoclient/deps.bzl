load("@bazel_gazelle//:deps.bzl", "go_repository")

def go_dependencies():
    go_repository(
        name = "co_honnef_go_tools",
        importpath = "honnef.co/go/tools",
        sum = "h1:/hemPrYIhOhy8zYrNj+069zDB68us2sMGsfkFJO0iZs=",
        version = "v0.0.0-20190523083050-ea95bdfd59fc",
    )
    go_repository(
        name = "com_github_antihax_optional",
        importpath = "github.com/antihax/optional",
        sum = "h1:xK2lYat7ZLaVVcIuj82J8kIro4V6kDe0AUDFboUCwcg=",
        version = "v1.0.0",
    )
    go_repository(
        name = "com_github_burntsushi_toml",
        importpath = "github.com/BurntSushi/toml",
        sum = "h1:WXkYYl6Yr3qBf1K79EBnL4mak0OimBfB0XUf9Vl28OQ=",
        version = "v0.3.1",
    )
    go_repository(
        name = "com_github_cenkalti_backoff_v4",
        importpath = "github.com/cenkalti/backoff/v4",
        sum = "h1:MyRJ/UdXutAwSAT+s3wNd7MfTIcy71VQueUuFK343L8=",
        version = "v4.3.0",
    )
    go_repository(
        name = "com_github_census_instrumentation_opencensus_proto",
        importpath = "github.com/census-instrumentation/opencensus-proto",
        sum = "h1:iKLQ0xPNFxR/2hzXZMrBo8f1j86j5WHzznCCQxV/b8g=",
        version = "v0.4.1",
    )
    go_repository(
        name = "com_github_cespare_xxhash",
        importpath = "github.com/cespare/xxhash",
        sum = "h1:a6HrQnmkObjyL+Gs60czilIUGqrzKutQD6XZog3p+ko=",
        version = "v1.1.0",
    )
    go_repository(
        name = "com_github_cespare_xxhash_v2",
        importpath = "github.com/cespare/xxhash/v2",
        sum = "h1:DC2CZ1Ep5Y4k3ZQ899DldepgrayRUGE6BBZ/cd9Cj44=",
        version = "v2.2.0",
    )
    go_repository(
        name = "com_github_client9_misspell",
        importpath = "github.com/client9/misspell",
        sum = "h1:ta993UF76GwbvJcIo3Y68y/M3WxlpEHPWIGDkJYwzJI=",
        version = "v0.3.4",
    )
    go_repository(
        name = "com_github_cncf_udpa_go",
        importpath = "github.com/cncf/udpa/go",
        sum = "h1:QQ3GSy+MqSHxm/d8nCtnAiZdYFd45cYZPs8vOOIYKfk=",
        version = "v0.0.0-20220112060539-c52dc94e7fbe",
    )
    go_repository(
        name = "com_github_cncf_xds_go",
        importpath = "github.com/cncf/xds/go",
        sum = "h1:/inchEIKaYC1Akx+H+gqO04wryn5h75LSazbRlnya1k=",
        version = "v0.0.0-20230607035331-e9ce68804cb4",
    )
    go_repository(
        name = "com_github_davecgh_go_spew",
        importpath = "github.com/davecgh/go-spew",
        sum = "h1:vj9j/u1bqnvCEfJOwUhtlOARqs3+rkHYY13jYWTU97c=",
        version = "v1.1.1",
    )
    go_repository(
        name = "com_github_distribution_reference",
        importpath = "github.com/distribution/reference",
        sum = "h1:/FUIFXtfc/x2gpa5/VGfiGLuOIdYa1t65IKK2OFGvA0=",
        version = "v0.5.0",
    )
    go_repository(
        name = "com_github_docker_distribution",
        importpath = "github.com/docker/distribution",
        sum = "h1:AtKxIZ36LoNK51+Z6RpzLpddBirtxJnzDrHLEKxTAYk=",
        version = "v2.8.3+incompatible",
    )
    go_repository(
        name = "com_github_docker_docker",
        importpath = "github.com/docker/docker",
        sum = "h1:HPGzNmwfLZWdxHqK9/II92pyi1EpYKsAqcl4G0Of9v0=",
        version = "v24.0.9+incompatible",
    )
    go_repository(
        name = "com_github_docker_go_connections",
        importpath = "github.com/docker/go-connections",
        sum = "h1:USnMq7hx7gwdVZq1L49hLXaFtUdTADjXGp+uj1Br63c=",
        version = "v0.5.0",
    )
    go_repository(
        name = "com_github_docker_go_units",
        importpath = "github.com/docker/go-units",
        sum = "h1:69rxXcBk27SvSaaxTtLh/8llcHD8vYHT7WSdRZ/jvr4=",
        version = "v0.5.0",
    )
    go_repository(
        name = "com_github_envoyproxy_go_control_plane",
        importpath = "github.com/envoyproxy/go-control-plane",
        sum = "h1:7T++XKzy4xg7PKy+bM+Sa9/oe1OC88yz2hXQUISoXfA=",
        version = "v0.11.1-0.20230524094728-9239064ad72f",
    )
    go_repository(
        name = "com_github_envoyproxy_protoc_gen_validate",
        importpath = "github.com/envoyproxy/protoc-gen-validate",
        sum = "h1:c0g45+xCJhdgFGw7a5QAfdS4byAbud7miNWJ1WwEVf8=",
        version = "v0.10.1",
    )
    go_repository(
        name = "com_github_ghodss_yaml",
        importpath = "github.com/ghodss/yaml",
        sum = "h1:wQHKEahhL6wmXdzwWG11gIVCkOv05bNOh+Rxn0yngAk=",
        version = "v1.0.0",
    )
    go_repository(
        name = "com_github_gogo_protobuf",
        importpath = "github.com/gogo/protobuf",
        sum = "h1:Ov1cvc58UF3b5XjBnZv7+opcTcQFZebYjWzi34vdm4Q=",
        version = "v1.3.2",
    )
    go_repository(
        name = "com_github_golang_glog",
        importpath = "github.com/golang/glog",
        sum = "h1:VKtxabqXZkF25pY9ekfRL6a582T4P37/31XEstQ5p58=",
        version = "v0.0.0-20160126235308-23def4e6c14b",
    )
    go_repository(
        name = "com_github_golang_groupcache",
        importpath = "github.com/golang/groupcache",
        sum = "h1:oI5xCqsCo564l8iNU+DwB5epxmsaqB+rhGL0m5jtYqE=",
        version = "v0.0.0-20210331224755-41bb18bfe9da",
    )
    go_repository(
        name = "com_github_golang_jwt_jwt",
        importpath = "github.com/golang-jwt/jwt",
        sum = "h1:IfV12K8xAKAnZqdXVzCZ+TOjboZ2keLg81eXfW3O+oY=",
        version = "v3.2.2+incompatible",
    )
    go_repository(
        name = "com_github_golang_mock",
        importpath = "github.com/golang/mock",
        sum = "h1:G5FRp8JnTd7RQH5kemVNlMeyXQAztQ3mOWV95KxsXH8=",
        version = "v1.1.1",
    )
    go_repository(
        name = "com_github_golang_protobuf",
        importpath = "github.com/golang/protobuf",
        sum = "h1:KhyjKVUg7Usr/dYsdSqoFveMYd5ko72D+zANwlG1mmg=",
        version = "v1.5.3",
    )
    go_repository(
        name = "com_github_google_cloud_android_orchestration",
        importpath = "github.com/google/cloud-android-orchestration",
        sum = "h1:Od84sE+n+8M3MoWv35U/+leGnEAKXADR+7GFuVod8L0=",
        version = "v0.1.0-alpha",
    )
    go_repository(
        name = "com_github_google_go_cmp",
        importpath = "github.com/google/go-cmp",
        sum = "h1:O2Tfq5qg4qc4AmwVlvv0oLiVAGB7enBSJ2x2DqQFi38=",
        version = "v0.5.9",
    )
    go_repository(
        name = "com_github_google_s2a_go",
        importpath = "github.com/google/s2a-go",
        sum = "h1:3Qm0liEiCErViKERO2Su5wp+9PfMRiuS6XB5FvpKnYQ=",
        version = "v0.1.0",
    )
    go_repository(
        name = "com_github_google_uuid",
        importpath = "github.com/google/uuid",
        sum = "h1:KjJaJ9iWZ3jOFZIf1Lqf4laDRCasjl0BCmnEGxkdLb4=",
        version = "v1.3.1",
    )
    go_repository(
        name = "com_github_googleapis_enterprise_certificate_proxy",
        importpath = "github.com/googleapis/enterprise-certificate-proxy",
        sum = "h1:yk9/cqRKtT9wXZSsRH9aurXEpJX+U6FLtpYTdC3R06k=",
        version = "v0.2.3",
    )
    go_repository(
        name = "com_github_googleapis_gax_go_v2",
        importpath = "github.com/googleapis/gax-go/v2",
        sum = "h1:UBtEZqx1bjXtOQ5BVTkuYghXrr3N4V123VKJK67vJZc=",
        version = "v2.8.0",
    )
    go_repository(
        name = "com_github_gorilla_mux",
        importpath = "github.com/gorilla/mux",
        sum = "h1:i40aqfkR1h2SlN9hojwV5ZA91wcXFOvkdNIeFDP5koI=",
        version = "v1.8.0",
    )
    go_repository(
        name = "com_github_gorilla_websocket",
        importpath = "github.com/gorilla/websocket",
        sum = "h1:saDtZ6Pbx/0u+bgYQ3q96pZgCzfhKXGPqt7kZ72aNNg=",
        version = "v1.5.3",
    )
    go_repository(
        name = "com_github_grpc_ecosystem_grpc_gateway",
        importpath = "github.com/grpc-ecosystem/grpc-gateway",
        sum = "h1:gmcG1KaJ57LophUzW0Hy8NmPhnMZb4M0+kPpLofRdBo=",
        version = "v1.16.0",
    )
    go_repository(
        name = "com_github_hashicorp_errwrap",
        importpath = "github.com/hashicorp/errwrap",
        sum = "h1:hLrqtEDnRye3+sgx6z4qVLNuviH3MR5aQ0ykNJa/UYA=",
        version = "v1.0.0",
    )
    go_repository(
        name = "com_github_hashicorp_go_multierror",
        importpath = "github.com/hashicorp/go-multierror",
        sum = "h1:H5DkEtf6CXdFp0N0Em5UCwQpXMWke8IA0+lD48awMYo=",
        version = "v1.1.1",
    )
    go_repository(
        name = "com_github_inconshreveable_mousetrap",
        importpath = "github.com/inconshreveable/mousetrap",
        sum = "h1:U3uMjPSQEBMNp1lFxmllqCPM6P5u/Xq7Pgzkat/bFNc=",
        version = "v1.0.1",
    )
    go_repository(
        name = "com_github_kr_pretty",
        importpath = "github.com/kr/pretty",
        sum = "h1:L/CwN0zerZDmRFUapSPitk6f+Q3+0za1rQkzVuMiMFI=",
        version = "v0.1.0",
    )
    go_repository(
        name = "com_github_kr_pty",
        importpath = "github.com/kr/pty",
        sum = "h1:VkoXIwSboBpnk99O/KFauAEILuNHv5DVFKZMBN/gUgw=",
        version = "v1.1.1",
    )
    go_repository(
        name = "com_github_kr_text",
        importpath = "github.com/kr/text",
        sum = "h1:5Nx0Ya0ZqY2ygV366QzturHI13Jq95ApcVaJBhpS+AY=",
        version = "v0.2.0",
    )
    go_repository(
        name = "com_github_microsoft_go_winio",
        importpath = "github.com/Microsoft/go-winio",
        sum = "h1:+hMXMk01us9KgxGb7ftKQt2Xpf5hH/yky+TDA+qxleU=",
        version = "v0.4.14",
    )
    go_repository(
        name = "com_github_moby_term",
        importpath = "github.com/moby/term",
        sum = "h1:xt8Q1nalod/v7BqbG21f8mQPqH+xAaC9C3N3wfWbVP0=",
        version = "v0.5.0",
    )
    go_repository(
        name = "com_github_morikuni_aec",
        importpath = "github.com/morikuni/aec",
        sum = "h1:nP9CBfwrvYnBRgY6qfDQkygYDmYwOilePFkwzv4dU8A=",
        version = "v1.0.0",
    )
    go_repository(
        name = "com_github_oneofone_xxhash",
        importpath = "github.com/OneOfOne/xxhash",
        sum = "h1:KMrpdQIwFcEqXDklaen+P1axHaj9BSKzvpUUfnHldSE=",
        version = "v1.2.2",
    )
    go_repository(
        name = "com_github_onsi_ginkgo",
        importpath = "github.com/onsi/ginkgo",
        sum = "h1:8xi0RTUf59SOSfEtZMvwTvXYMzG4gV23XVHOZiXNtnE=",
        version = "v1.16.5",
    )
    go_repository(
        name = "com_github_onsi_gomega",
        importpath = "github.com/onsi/gomega",
        sum = "h1:9Luw4uT5HTjHTN8+aNcSThgH1vdXnmdJ8xIfZ4wyTRE=",
        version = "v1.17.0",
    )
    go_repository(
        name = "com_github_opencontainers_go_digest",
        importpath = "github.com/opencontainers/go-digest",
        sum = "h1:apOUWs51W5PlhuyGyz9FCeeBIOUDA/6nW8Oi/yOhh5U=",
        version = "v1.0.0",
    )
    go_repository(
        name = "com_github_opencontainers_image_spec",
        importpath = "github.com/opencontainers/image-spec",
        sum = "h1:8SG7/vwALn54lVB/0yZ/MMwhFrPYtpEHQb2IpWsCzug=",
        version = "v1.1.0",
    )
    go_repository(
        name = "com_github_paesslerag_gval",
        importpath = "github.com/PaesslerAG/gval",
        sum = "h1:GEKnRwkWDdf9dOmKcNrar9EA1bz1z9DqPIO1+iLzhd8=",
        version = "v1.0.0",
    )
    go_repository(
        name = "com_github_paesslerag_jsonpath",
        importpath = "github.com/PaesslerAG/jsonpath",
        sum = "h1:c1/AToHQMVsduPAa4Vh6xp2U0evy4t8SWp8imEsylIk=",
        version = "v0.1.1",
    )
    go_repository(
        name = "com_github_pelletier_go_toml",
        importpath = "github.com/pelletier/go-toml",
        sum = "h1:4yBQzkHv+7BHq2PQUZF3Mx0IYxG7LsP222s7Agd3ve8=",
        version = "v1.9.5",
    )
    go_repository(
        name = "com_github_pion_datachannel",
        importpath = "github.com/pion/datachannel",
        sum = "h1:ph1P1NsGkazkjrvyMfhRBUAWMxugJjq2HfQifaOoSNo=",
        version = "v1.5.8",
    )
    go_repository(
        name = "com_github_pion_dtls_v2",
        importpath = "github.com/pion/dtls/v2",
        sum = "h1:KP7H5/c1EiVAAKUmXyCzPiQe5+bCJrpOeKg/L05dunk=",
        version = "v2.2.12",
    )
    go_repository(
        name = "com_github_pion_ice_v2",
        importpath = "github.com/pion/ice/v2",
        sum = "h1:SopeXiVbbcooUg2EIR8sq4b13RQ8gzrkkldOVg+bBsc=",
        version = "v2.3.36",
    )
    go_repository(
        name = "com_github_pion_interceptor",
        importpath = "github.com/pion/interceptor",
        sum = "h1:39fsnlP1U8gw2JzOFWdfCU82vHvhW9o0rZnZF56wF+M=",
        version = "v0.1.29",
    )
    go_repository(
        name = "com_github_pion_logging",
        importpath = "github.com/pion/logging",
        sum = "h1:M9+AIj/+pxNsDfAT64+MAVgJO0rsyLnoJKCqf//DoeY=",
        version = "v0.2.2",
    )
    go_repository(
        name = "com_github_pion_mdns",
        importpath = "github.com/pion/mdns",
        sum = "h1:CiMYlY+O0azojWDmxdNr7ADGrnZ+V6Ilfner+6mSVK8=",
        version = "v0.0.12",
    )
    go_repository(
        name = "com_github_pion_randutil",
        importpath = "github.com/pion/randutil",
        sum = "h1:CFG1UdESneORglEsnimhUjf33Rwjubwj6xfiOXBa3mA=",
        version = "v0.1.0",
    )
    go_repository(
        name = "com_github_pion_rtcp",
        importpath = "github.com/pion/rtcp",
        sum = "h1:KCkGV3vJ+4DAJmvP0vaQShsb0xkRfWkO540Gy102KyE=",
        version = "v1.2.14",
    )
    go_repository(
        name = "com_github_pion_rtp",
        importpath = "github.com/pion/rtp",
        sum = "h1:qslKkG8qxvQ7hqaxkmL7Pl0XcUm+/Er7nMnu6Vq+ZxM=",
        version = "v1.8.7",
    )
    go_repository(
        name = "com_github_pion_sctp",
        importpath = "github.com/pion/sctp",
        sum = "h1:2CYuw+SQ5vkQ9t0HdOPccsCz1GQMDuVy5PglLgKVBW8=",
        version = "v1.8.19",
    )
    go_repository(
        name = "com_github_pion_sdp_v3",
        importpath = "github.com/pion/sdp/v3",
        sum = "h1:pX++dCHoHUwq43kuwf3PyJfHlwIj4hXA7Vrifiq0IJY=",
        version = "v3.0.9",
    )
    go_repository(
        name = "com_github_pion_srtp_v2",
        importpath = "github.com/pion/srtp/v2",
        sum = "h1:HNNny4s+OUmG280ETrCdgFndp4ufx3/uy85EawYEhTk=",
        version = "v2.0.20",
    )
    go_repository(
        name = "com_github_pion_stun",
        importpath = "github.com/pion/stun",
        sum = "h1:8lp6YejULeHBF8NmV8e2787BogQhduZugh5PdhDyyN4=",
        version = "v0.6.1",
    )
    go_repository(
        name = "com_github_pion_transport",
        importpath = "github.com/pion/transport",
        sum = "h1:/UH5yLeQtwm2VZIPjxwnNFxjS4DFhyLfS4GlfuKUzfA=",
        version = "v0.13.1",
    )
    go_repository(
        name = "com_github_pion_transport_v2",
        importpath = "github.com/pion/transport/v2",
        sum = "h1:ucLBLE8nuxiHfvkFKnkDQRYWYfp8ejf4YBOPfaQpw6Q=",
        version = "v2.2.10",
    )
    go_repository(
        name = "com_github_pion_transport_v3",
        importpath = "github.com/pion/transport/v3",
        sum = "h1:r+40RJR25S9w3jbA6/5uEPTzcdn7ncyU44RWCbHkLg4=",
        version = "v3.0.2",
    )
    go_repository(
        name = "com_github_pion_turn_v2",
        importpath = "github.com/pion/turn/v2",
        sum = "h1:Xr2niVsiPTB0FPtt+yAWKFUkU1eotQbGgpTIld4x1Gc=",
        version = "v2.1.6",
    )
    go_repository(
        name = "com_github_pion_udp",
        importpath = "github.com/pion/udp",
        sum = "h1:OowsTmu1Od3sD6i3fQUJxJn2fEvJO6L1TidgadtbTI8=",
        version = "v0.1.4",
    )
    go_repository(
        name = "com_github_pion_webrtc_v3",
        importpath = "github.com/pion/webrtc/v3",
        sum = "h1:v2heQVnXTSqNRXcaFQVOhIOYkLMxOu1iJG8uy1djvkk=",
        version = "v3.3.4",
    )
    go_repository(
        name = "com_github_pkg_errors",
        importpath = "github.com/pkg/errors",
        sum = "h1:FEBLx1zS214owpjy7qsBeixbURkuhQAwrK5UwLGTwt4=",
        version = "v0.9.1",
    )
    go_repository(
        name = "com_github_pmezard_go_difflib",
        importpath = "github.com/pmezard/go-difflib",
        sum = "h1:4DBwDE0NGyQoBHbLQYPwSUPoCMWR5BEzIk/f1lZbAQM=",
        version = "v1.0.0",
    )
    go_repository(
        name = "com_github_prometheus_client_model",
        importpath = "github.com/prometheus/client_model",
        sum = "h1:gQz4mCbXsO+nc9n1hCxHcGA3Zx3Eo+UHZoInFGUIXNM=",
        version = "v0.0.0-20190812154241-14fe0d1b01d4",
    )
    go_repository(
        name = "com_github_rogpeppe_fastuuid",
        importpath = "github.com/rogpeppe/fastuuid",
        sum = "h1:Ppwyp6VYCF1nvBTXL3trRso7mXMlRrw9ooo375wvi2s=",
        version = "v1.2.0",
    )
    go_repository(
        name = "com_github_sclevine_agouti",
        importpath = "github.com/sclevine/agouti",
        sum = "h1:8IBJS6PWz3uTlMP3YBIR5f+KAldcGuOeFkFbUWfBgK4=",
        version = "v3.0.0+incompatible",
    )
    go_repository(
        name = "com_github_sergi_go_diff",
        importpath = "github.com/sergi/go-diff",
        sum = "h1:XU+rvMAioB0UC3q1MFrIQy4Vo5/4VsRDQQXHsEya6xQ=",
        version = "v1.2.0",
    )
    go_repository(
        name = "com_github_spaolacci_murmur3",
        importpath = "github.com/spaolacci/murmur3",
        sum = "h1:qLC7fQah7D6K1B0ujays3HV9gkFtllcxhzImRR7ArPQ=",
        version = "v0.0.0-20180118202830-f09979ecbc72",
    )
    go_repository(
        name = "com_github_spf13_cobra",
        importpath = "github.com/spf13/cobra",
        sum = "h1:o94oiPyS4KD1mPy2fmcYYHHfCxLqYjJOhGsCHFZtEzA=",
        version = "v1.6.1",
    )
    go_repository(
        name = "com_github_spf13_pflag",
        importpath = "github.com/spf13/pflag",
        sum = "h1:iy+VFUOCP1a+8yFto/drg2CJ5u0yRoB7fZw3DKv/JXA=",
        version = "v1.0.5",
    )
    go_repository(
        name = "com_github_stretchr_objx",
        importpath = "github.com/stretchr/objx",
        sum = "h1:xuMeJ0Sdp5ZMRXx/aWO6RZxdr3beISkG5/G/aIRr3pY=",
        version = "v0.5.2",
    )
    go_repository(
        name = "com_github_stretchr_testify",
        importpath = "github.com/stretchr/testify",
        sum = "h1:HtqpIVDClZ4nwg75+f6Lvsy/wHu+3BoSGCbBAcpTsTg=",
        version = "v1.9.0",
    )
    go_repository(
        name = "com_github_wlynxg_anet",
        importpath = "github.com/wlynxg/anet",
        sum = "h1:PvR53psxFXstc12jelG6f1Lv4MWqE0tI76/hHGjh9rg=",
        version = "v0.0.3",
    )
    go_repository(
        name = "com_github_yuin_goldmark",
        importpath = "github.com/yuin/goldmark",
        sum = "h1:fVcFKWvrslecOb/tg+Cc05dkeYx540o0FuFt3nUVDoE=",
        version = "v1.4.13",
    )
    go_repository(
        name = "com_google_cloud_go",
        importpath = "cloud.google.com/go",
        sum = "h1:Zc8gqp3+a9/Eyph2KDmcGaPtbKRIoqq4YTlL4NMD0Ys=",
        version = "v0.110.0",
    )
    go_repository(
        name = "com_google_cloud_go_compute",
        importpath = "cloud.google.com/go/compute",
        sum = "h1:am86mquDUgjGNWxiGn+5PGLbmgiWXlE/yNWpIpNvuXY=",
        version = "v1.19.1",
    )
    go_repository(
        name = "com_google_cloud_go_compute_metadata",
        importpath = "cloud.google.com/go/compute/metadata",
        sum = "h1:mg4jlk7mCAj6xXp9UJ4fjI9VUI5rubuGBW5aJ7UnBMY=",
        version = "v0.2.3",
    )
    go_repository(
        name = "com_google_cloud_go_iam",
        importpath = "cloud.google.com/go/iam",
        sum = "h1:+CmB+K0J/33d0zSQ9SlFWUeCCEn5XJA0ZMZ3pHE9u8k=",
        version = "v0.13.0",
    )
    go_repository(
        name = "com_google_cloud_go_kms",
        importpath = "cloud.google.com/go/kms",
        sum = "h1:8UePKEypK3SQ6g+4mn/s/VgE5L7XOh+FwGGRUqvY3Hw=",
        version = "v1.10.2",
    )
    go_repository(
        name = "com_google_cloud_go_secretmanager",
        importpath = "cloud.google.com/go/secretmanager",
        sum = "h1:9QwQ3oMurvmPEmM80spGe2SFGDa+RRgkLIdTm3gMWO8=",
        version = "v1.10.1",
    )
    go_repository(
        name = "com_google_cloud_go_spanner",
        importpath = "cloud.google.com/go/spanner",
        sum = "h1:vHFqBMuPdTCwA8b9+IyQbGppQoqx7xJfcSa81d7gtAk=",
        version = "v1.45.1",
    )
    go_repository(
        name = "in_gopkg_check_v1",
        importpath = "gopkg.in/check.v1",
        sum = "h1:YR8cESwS4TdDjEe65xsg0ogRM/Nc3DYOhEAlW+xobZo=",
        version = "v1.0.0-20190902080502-41f04d3bba15",
    )
    go_repository(
        name = "in_gopkg_yaml_v2",
        importpath = "gopkg.in/yaml.v2",
        sum = "h1:fvjTMHxHEw/mxHbtzPi3JCcKXQRAnQTBRo6YCJSVHKI=",
        version = "v2.2.3",
    )
    go_repository(
        name = "in_gopkg_yaml_v3",
        importpath = "gopkg.in/yaml.v3",
        sum = "h1:fxVm/GzAzEWqLHuvctI91KS9hhNmmWOoWu0XTYJS7CA=",
        version = "v3.0.1",
    )
    go_repository(
        name = "io_opencensus_go",
        importpath = "go.opencensus.io",
        sum = "h1:y73uSU6J157QMP2kn2r30vwW1A2W2WFwSCGnAVxeaD0=",
        version = "v0.24.0",
    )
    go_repository(
        name = "io_opentelemetry_go_proto_otlp",
        importpath = "go.opentelemetry.io/proto/otlp",
        sum = "h1:rwOQPCuKAKmwGKq2aVNnYIibI6wnV7EvzgfTCzcdGg8=",
        version = "v0.7.0",
    )
    go_repository(
        name = "org_golang_google_api",
        importpath = "google.golang.org/api",
        sum = "h1:FNfHq9Z2GKULxu7cEhCaB0wWQHg43UpomrrN+24ZRdE=",
        version = "v0.118.0",
    )
    go_repository(
        name = "org_golang_google_appengine",
        importpath = "google.golang.org/appengine",
        sum = "h1:FZR1q0exgwxzPzp/aF+VccGrSfxfPpkBqjIIEq3ru6c=",
        version = "v1.6.7",
    )
    go_repository(
        name = "org_golang_google_genproto",
        importpath = "google.golang.org/genproto",
        sum = "h1:KpwkzHKEF7B9Zxg18WzOa7djJ+Ha5DzthMyZYQfEn2A=",
        version = "v0.0.0-20230410155749-daa745c078e1",
    )
    go_repository(
        name = "org_golang_google_grpc",
        importpath = "google.golang.org/grpc",
        sum = "h1:8I4C0Yq1EjstUzUJzpcRVbuYA2mODtEmpWiQoN/b2nc=",
        version = "v1.56.3",
    )
    go_repository(
        name = "org_golang_google_protobuf",
        importpath = "google.golang.org/protobuf",
        sum = "h1:uNO2rsAINq/JlFpSdYEKIZ0uKD/R9cpdv0T+yoGwGmI=",
        version = "v1.33.0",
    )
    go_repository(
        name = "org_golang_x_crypto",
        importpath = "golang.org/x/crypto",
        sum = "h1:X31++rzVUdKhX5sWmSOFZxx8UW/ldWx55cbf08iNAMA=",
        version = "v0.21.0",
    )
    go_repository(
        name = "org_golang_x_exp",
        importpath = "golang.org/x/exp",
        sum = "h1:c2HOrn5iMezYjSlGPncknSEr/8x5LELb/ilJbXi9DEA=",
        version = "v0.0.0-20190121172915-509febef88a4",
    )
    go_repository(
        name = "org_golang_x_lint",
        importpath = "golang.org/x/lint",
        sum = "h1:XQyxROzUlZH+WIQwySDgnISgOivlhjIEwaQaJEJrrN0=",
        version = "v0.0.0-20190313153728-d0100b6bd8b3",
    )
    go_repository(
        name = "org_golang_x_mod",
        importpath = "golang.org/x/mod",
        sum = "h1:LUYupSeNrTNCGzR/hVBk2NHZO4hXcVaW1k4Qx7rjPx8=",
        version = "v0.8.0",
    )
    go_repository(
        name = "org_golang_x_net",
        importpath = "golang.org/x/net",
        sum = "h1:7EYJ93RZ9vYSZAIb2x3lnuvqO5zneoD6IvWjuhfxjTs=",
        version = "v0.23.0",
    )
    go_repository(
        name = "org_golang_x_oauth2",
        importpath = "golang.org/x/oauth2",
        sum = "h1:6dkIjl3j3LtZ/O3sTgZTMsLKSftL/B8Zgq4huOIIUu8=",
        version = "v0.8.0",
    )
    go_repository(
        name = "org_golang_x_sync",
        importpath = "golang.org/x/sync",
        sum = "h1:wsuoTGHzEhffawBOhz5CYhcrV4IdKZbEyZjBMuTp12o=",
        version = "v0.1.0",
    )
    go_repository(
        name = "org_golang_x_sys",
        importpath = "golang.org/x/sys",
        sum = "h1:DBdB3niSjOA/O0blCZBqDefyWNYveAYMNF1Wum0DYQ4=",
        version = "v0.18.0",
    )
    go_repository(
        name = "org_golang_x_term",
        importpath = "golang.org/x/term",
        sum = "h1:FcHjZXDMxI8mM3nwhX9HlKop4C0YQvCVCdwYl2wOtE8=",
        version = "v0.18.0",
    )
    go_repository(
        name = "org_golang_x_text",
        importpath = "golang.org/x/text",
        sum = "h1:ScX5w1eTa3QqT8oi6+ziP7dTV1S2+ALU0bI+0zXKWiQ=",
        version = "v0.14.0",
    )
    go_repository(
        name = "org_golang_x_time",
        importpath = "golang.org/x/time",
        sum = "h1:o7cqy6amK/52YcAKIPlM3a+Fpj35zvRj2TP+e1xFSfk=",
        version = "v0.5.0",
    )
    go_repository(
        name = "org_golang_x_tools",
        importpath = "golang.org/x/tools",
        sum = "h1:BOw41kyTf3PuCW1pVQf8+Cyg8pMlkYB1oo9iJ6D/lKM=",
        version = "v0.6.0",
    )
    go_repository(
        name = "org_golang_x_xerrors",
        importpath = "golang.org/x/xerrors",
        sum = "h1:H2TDz8ibqkAF6YGhCdN3jS9O0/s90v0rJh3X/OLHEUk=",
        version = "v0.0.0-20220907171357-04be3eba64a2",
    )
    go_repository(
        name = "tools_gotest_v3",
        importpath = "gotest.tools/v3",
        sum = "h1:EENdUnS3pdur5nybKYIh2Vfgc8IUNBjxDPSjtiJcOzU=",
        version = "v3.5.1",
    )
