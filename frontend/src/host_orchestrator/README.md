# Host Orchestrator

## API Documentation

The Host Orchestrator API documentation is generated with the [swag](https://github.com/swaggo/swag) tool,
generating OpenAPI Specifications based on Go annotations.

## See documentation

Use https://editor.swagger.io/ copying the content docs/swagger.yaml for an interactive visualization
of the documentation.


## Update documentation 

Install swag

```
go install github.com/swaggo/swag/cmd/swag@latest
```

Generate updated documentation

```
$(go env GOPATH)/bin/swag init --outputTypes yaml
```

