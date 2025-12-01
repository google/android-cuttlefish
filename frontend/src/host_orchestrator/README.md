# Host Orchestrator

## API Documentation

The Host Orchestrator API documentation is generated with the [swag](https://github.com/swaggo/swag) tool,
generating OpenAPI Specifications based on Go annotations.

Visit 
[Web View](https://petstore3.swagger.io/?url=https://raw.githubusercontent.com/google/android-cuttlefish/refs/heads/main/frontend/src/host_orchestrator/docs/swagger.yaml)
to see documentation derived from [docs/swagger.yaml](docs/swagger.yaml).

## Update documentation 

Install swag

```
go install github.com/swaggo/swag/cmd/swag@latest
```

Generate updated documentation

```
# run in `android-cuttlefish/frontend/src/host_orchestrator` directory
$(go env GOPATH)/bin/swag init --outputTypes yaml
```

