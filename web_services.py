from spyne import Application, rpc, ServiceBase, String
from spyne.protocol.soap import Soap11
from spyne.server.wsgi import WsgiApplication
from datetime import datetime


class DateTimeService(ServiceBase):
    @rpc(_returns=String)
    def get_current_datetime(ctx):
        # Devolver fecha actual
        return datetime.now().strftime('%d/%m/%Y %H:%M:%S')

application = Application([DateTimeService],
                          tns='spyne.examples.datetime',
                          in_protocol=Soap11(validator='lxml'),
                          out_protocol=Soap11())

if __name__ == '__main__':
    from wsgiref.simple_server import make_server
    # Crea un servicio SOAP simple que utiliza WSGI para escuchar solicitudes en localhost en el puerto 8000
    server = make_server('127.0.0.1', 8000, WsgiApplication(application))
    server.serve_forever()
