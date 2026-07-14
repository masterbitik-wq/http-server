# HTTP Server (Windows, select)
Асинхронный HTTP-сервер на C11 с использованием Windows Sockets и `select()`. Раздаёт статические файлы из указанной корневой директории.

## Сборка
make

## Использование
./http_server <addr:port> <root_dir>

## Пример:
./http_server 0.0.0.0:8080 ./www

## Проверка
Откройте браузер и перейдите по адресу:
http://localhost:8080/
или выполните curl:
curl http://localhost:8080/
