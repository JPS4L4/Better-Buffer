# Better Buffer

## Introducción

Better Buffer es un plugin para OBS Studio que reemplaza el buffer de repetición nativo por uno **por fuente**: se aplica como filtro directamente sobre la salida de video que quieras (cámara, captura de pantalla, ventana, etc.), con su propio encoder, bitrate y pista de audio configurables — pensado para generar clips ya listos para editar y subir a redes, no solo un respaldo genérico de la escena completa.

El botón/hotkey nativo de "Guardar Repetición" de OBS sigue siendo el disparador — Better Buffer no reemplaza esa interacción, solo lo que ocurre detrás: cada fuente con el filtro aplicado guarda su propio clip aislado, con la calidad y configuración que definas para esa fuente en particular.

Este proyecto está construido sobre el [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) oficial de OBS Project.

## Entornos de compilación soportados

| Plataforma       | Herramienta            |
| ---------------- | ----------------------- |
| Windows          | Visual Studio 17 2022    |
| macOS            | XCode 16.0                |
| Windows, macOS   | CMake 3.30.5              |
| Ubuntu 24.04     | CMake 3.28.3               |
| Ubuntu 24.04     | `ninja-build`               |
| Ubuntu 24.04     | `pkg-config`                 |
| Ubuntu 24.04     | `build-essential`             |

## Inicio rápido

Si vas a compilar Better Buffer desde el código fuente, revisa [`SETUP.md`](SETUP.md) para el stack de instalación y el ciclo de compilación en VS Code.

Si solo quieres instalar el plugin desde el repositorio, revisa [`install.md`](install.md).

Para usuarios que solo quieren instalar y usar el plugin, revisa el [Manual de Usuario de Better Buffer](MANUAL.md).

## Documentación

* [Manual de Usuario de Better Buffer](MANUAL.md) — cómo instalar y usar el plugin.
* [Guía rápida de instalación](install.md) — instalación directa para usuarios finales.
* [Roadmap técnico](ROADMAP.md) — arquitectura y fases de desarrollo, útil si quieres contribuir.
* [Guía de instalación del entorno de desarrollo](SETUP.md)

Documentación general de la plantilla base (referencia para quien quiera entender el sistema de build):
* [Quick Start Guide](https://github.com/obsproject/obs-plugintemplate/wiki/Quick-Start-Guide)
* [Getting started](https://github.com/obsproject/obs-plugintemplate/wiki/Getting-Started)
* [Build system requirements](https://github.com/obsproject/obs-plugintemplate/wiki/Build-System-Requirements)
* [Build system options](https://github.com/obsproject/obs-plugintemplate/wiki/CMake-Build-System-Options)

## Apoya el proyecto

Better Buffer es desarrollado y mantenido en mi tiempo libre. Si te resulta útil y quieres apoyar el desarrollo continuo (nuevas funciones, corrección de bugs, soporte en más plataformas), puedes invitarme un café:

<a href="https://www.buymeacoffee.com/R4zalas" target="_blank">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="41" width="174">
</a>

No es obligatorio en absoluto — usar y compartir el plugin ya es la mejor forma de apoyo. 🙂

## GitHub Actions & CI

Hay workflows por defecto de GitHub Actions disponibles para las siguientes acciones del repositorio:
* `push`: Corre para commits o tags enviados a las ramas `master` o `main`.
* `pr-pull`: Corre cuando un Pull Request se envía o sincroniza.
* `dispatch`: Corre cuando se dispara manualmente desde la interfaz de GitHub.
* `build-project`: Compila el proyecto y es disparado por otros workflows.
* `check-format`: Revisa el formato del código CMake y del plugin, disparado por otros workflows.

Estos workflows usan acciones de repositorio de GitHub (en `.github/actions`) y scripts de build (en `.github/scripts`) que no son necesarios para desarrollo local, pero podrían necesitar ajustes si se requieren pasos adicionales o distintos para compilar el plugin.

### Obtener artefactos de build

Los builds exitosos en GitHub Actions producen artefactos descargables para pruebas. Estos artefactos son archivos comprimidos simples y no incluyen instaladores.

### Crear un Release

Para crear un release, se debe enviar un tag con el nombre apropiado a la rama `main`/`master` usando versionado semántico (ej. `0.2.0`, `1.0.0-beta2`). Se creará un draft release en el repositorio asociado con los paquetes de instalación generados adjuntos como artefactos del release.

## Firma y notarización en macOS

Los conceptos básicos de firma de código y notarización en macOS están explicados en el [artículo de la Wiki correspondiente](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS), que tiene una sección específica para la [configuración de GitHub Actions](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS#setting-up-code-signing-for-github-actions).
