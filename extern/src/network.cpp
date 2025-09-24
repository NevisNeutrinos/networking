//
// Created by Jon Sensenig on 8/21/25.
//

#include <streambuf>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // Needed for automatic vector conversions
#include <pybind11/iostream.h>
#include "../../tcp_connection.h"

namespace py = pybind11;

class PythonStreamBuf : public std::streambuf {
public:
    PythonStreamBuf(py::object py_stdout) : py_stdout_(py_stdout) {}

protected:
    int_type overflow(int_type ch) override {
        if (ch != EOF) {
            py_stdout_.attr("write")(std::string(1, ch));
            py_stdout_.attr("flush")();
        }
        return ch;
    }

private:
    py::object py_stdout_;
};

// PYBIND11_MODULE defines a function that will be called when the Python module is imported.
// The first argument is the module name (must match the name in setup.py)
// The second argument (m) is a py::module_ object that is the main interface
PYBIND11_MODULE(network_module, m) {
    m.doc() = "Python bindings for the Network code"; // Optional module docstring

    // Grab Python sys.stdout and Create the streambuf and redirect std::cout
//    py::object py_stdout = py::module_::import("sys").attr("stdout");
//    static PythonStreamBuf buf(py_stdout);
//    std::cout.rdbuf(&buf);

    py::class_<asio::io_context>(m, "IOContext")
        .def(py::init<>())
//        .def("run", &asio::io_context::run)
        .def("run", static_cast<std::size_t (asio::io_context::*)()>(&asio::io_context::run))
        .def("stop", &asio::io_context::stop);

    // Since the TCPProtocol class inherits the Command class we have to also bind it
    // 1. Bind the base class FIRST
    py::class_<Command, std::shared_ptr<Command>>(m, "Command")
        // Bind the constructor
        .def(py::init<uint16_t, size_t>(),
             py::arg("cmd_code"), py::arg("arg_count"))

        .def_readwrite("command", &Command::command)
        .def_readwrite("arguments", &Command::arguments)

        // Bind the 'fill' method
        .def("set_arguments", &Command::SetArguments, "Set the arguments", py::arg("args"));


//    // Expose the TCPProtocol class to Python
//    py::class_<TCPProtocol, Command>(m, "TCPProtocol")
//        // Bind the constructor
//        .def(py::init<uint16_t, size_t>(),
//             py::arg("cmd_code"), py::arg("arg_count"))
//
//        // Bind the 'serialize' and 'deserialize' methods
//        .def("serialize", &TCPProtocol::Serialize, "Serializes the packet to a list of bytes")
//        .def("deserialize", &TCPProtocol::Deserialize, "De-Serializes the packet")
//
//        // Expose public member variables as read-only properties in Python
//        .def_readwrite("arg_count", &TCPProtocol::arg_count)
//
//        .def_property_readonly_static("kStartCode1", [](py::object /* self */) { return TCPProtocol::kStartCode1; })
//        .def_property_readonly_static("kStartCode2", [](py::object /* self */) { return TCPProtocol::kStartCode2; })
//        .def_property_readonly_static("kEndCode1", [](py::object /* self */) { return TCPProtocol::kEndCode1; })
//        .def_property_readonly_static("kEndCode2", [](py::object /* self */) { return TCPProtocol::kEndCode2; });


    py::class_<TCPConnection, std::shared_ptr<TCPConnection>, Command>(m, "TCPConnection")
        .def(py::init<asio::io_context&, const std::string&, uint16_t, bool, bool, bool>(),
             py::arg("io_context"),
             py::arg("ip_address"),
             py::arg("port"),
             py::arg("is_server"),
             py::arg("use_heartbeat"),
             py::arg("monitor_link"))

        // WriteSendBuffer(uint16_t, std::vector<int32_t>&)
        .def("write_send_buffer", [](TCPConnection &self, uint16_t cmd, const std::vector<int32_t> &vec) {
                 // copy is fine since Python -> C++ conversion yields a temporary anyway
                 std::vector<int32_t> copy = vec;
                 self.WriteSendBuffer(cmd, copy);
             },
             py::arg("cmd"), py::arg("args"))

        // WriteSendBuffer(const Command&)
        .def("write_send_buffer", [](TCPConnection &self, const Command &cmd) {
                 self.WriteSendBuffer(cmd);
             },
             py::arg("command"))

        // Overload: ReadRecvBuffer() -> Command
        .def("read_recv_buffer",
             [](TCPConnection &self) {
                 return self.ReadRecvBuffer();
             },
             "Read one Command from the receive buffer")

        // Overload: ReadRecvBuffer(size_t num_cmds) -> std::vector<Command>
        .def("read_recv_buffer",
             [](TCPConnection &self, size_t num_cmds) {
                 return self.ReadRecvBuffer(num_cmds);
             },
             py::arg("num_cmds"),
             "Read multiple Commands from the receive buffer")

        // socket open check
        .def("get_socket_is_open", &TCPConnection::getSocketIsOpen)

        // control read loop
        .def("set_stop_cmd_read", &TCPConnection::setStopCmdRead)

        .def("run_ctx", [](TCPConnection &self, asio::io_context &ctx) {
            self.PythonRun(ctx);
        })

        .def("stop_ctx", [](TCPConnection &self, asio::io_context &ctx) {
            self.PythonStop(ctx);
        });
}
