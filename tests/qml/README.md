# QML tests

`OpcUaManager.QmlSmoke` loads the `Base` QML module with a small mock
`cppManagerOpcUa` context property and verifies that `BsMenuBar`,
`BsOpcUaConnectionForm`, and `BsOpcUaBrowser` can be created.

Run all tests from the build directory:

```bash
ctest --output-on-failure -C Debug
```

The external OPC UA integration test is skipped unless this environment variable
is set:

```text
OPCUAMANAGER_TEST_SERVER_URL=opc.tcp://127.0.0.1:4840
```
