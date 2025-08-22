//
// Created by Jon Sensenig on 8/21/25.
//

#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // Needed for automatic vector conversions
#include "../../tcp_protocol.h"

namespace py = pybind11;

// PYBIND11_MODULE defines a function that will be called when the Python module is imported.
// The first argument is the module name (must match the name in setup.py)
// The second argument (m) is a py::module_ object that is the main interface
PYBIND11_MODULE(network_module, m) {
    m.doc() = "Python bindings for the Network code"; // Optional module docstring

    // Since the TCPProtocol class inherits the Command class we have to also bind it
    // 1. Bind the base class FIRST
    py::class_<Command>(m, "Command")
        // Bind the constructor
        .def(py::init<uint16_t, size_t>(),
             py::arg("cmd_code"), py::arg("arg_count"))

        .def_readwrite("command", &Command::command)
        .def_readwrite("arguments", &Command::arguments)

        // Bind the 'fill' method
        .def("set_arguments", &Command::SetArguments, "Set the arguments", py::arg("args"));


    // Expose the TCPProtocol class to Python
    py::class_<TCPProtocol, Command>(m, "TCPProtocol")
        // Bind the constructor
        .def(py::init<uint16_t, size_t>(),
             py::arg("cmd_code"), py::arg("arg_count"))

        // Bind the 'serialize' and 'deserialize' methods
        .def("serialize", &TCPProtocol::Serialize, "Serializes the packet to a list of bytes")
        .def("deserialize", &TCPProtocol::Deserialize, "De-Serializes the packet")

        // Expose public member variables as read-only properties in Python
        .def_readwrite("arg_count", &TCPProtocol::arg_count)

        .def_property_readonly_static("kStartCode1", [](py::object /* self */) { return TCPProtocol::kStartCode1; })
        .def_property_readonly_static("kStartCode2", [](py::object /* self */) { return TCPProtocol::kStartCode2; })
        .def_property_readonly_static("kEndCode1", [](py::object /* self */) { return TCPProtocol::kEndCode1; })
        .def_property_readonly_static("kEndCode2", [](py::object /* self */) { return TCPProtocol::kEndCode2; });

}
