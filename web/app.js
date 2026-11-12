'use strict';

(() => {
    const $ = (id) => document.getElementById(id);

    function numero(valor, decimales) {
        if (!Number.isFinite(valor)) return '—';
        const neg = valor < 0;
        const texto = Math.abs(valor).toFixed(decimales === undefined ? 0 : decimales);
        const partes = texto.split('.');
        partes[0] = partes[0].replace(/\B(?=(\d{3})+(?!\d))/g, '.');
        return (neg ? '-' : '') + partes.join(',');
    }

    function formatearBytes(bytes) {
        if (!Number.isFinite(bytes)) return '—';
        const neg = bytes < 0;
        let valor = Math.abs(bytes);
        if (valor === 0) return '0 B';
        const unidades = ['B', 'KB', 'MB', 'GB', 'TB'];
        let indice = 0;
        while (valor >= 1024 && indice < unidades.length - 1) {
            valor /= 1024;
            indice += 1;
        }
        const decimales = indice === 0 ? 0 : (valor >= 100 ? 0 : 1);
        return (neg ? '-' : '') + numero(valor, decimales) + ' ' + unidades[indice];
    }

    function formatearKb(kb) {
        return formatearBytes(kb * 1024);
    }

    function formatearEjeKb(kb) {
        if (kb >= 1048576) return numero(kb / 1048576, 1) + ' GB';
        if (kb >= 1024) return numero(kb / 1024, 0) + ' MB';
        return numero(kb, 0) + ' kB';
    }

    function formatearEjeNumero(valor) {
        if (valor >= 1e9) return numero(valor / 1e9, 1) + ' G';
        if (valor >= 1e6) return numero(valor / 1e6, 1) + ' M';
        if (valor >= 1000) return numero(valor / 1000, 0) + ' k';
        return numero(valor, 0);
    }

    function formatearDuracion(ms) {
        if (!Number.isFinite(ms)) return '—';
        if (ms < 1000) return numero(ms, 0) + ' ms';
        const segundos = ms / 1000;
        if (segundos < 60) return numero(segundos, 2) + ' s';
        const minutos = Math.floor(segundos / 60);
        return minutos + ' min ' + numero(segundos - minutos * 60, 1) + ' s';
    }

    function formatearPorcentaje(valor) {
        return numero(valor, 1) + ' %';
    }

    function formatearFecha(iso) {
        if (typeof iso !== 'string') return '—';
        const partes = iso.match(/^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})/);
        if (!partes) return iso;
        return partes[3] + '/' + partes[2] + ' ' + partes[4] + ':' + partes[5] + ':' + partes[6];
    }

    function escaparHtml(texto) {
        return String(texto)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#39;');
    }

    const NOMBRE_ESTADO = {
        inactiva: 'inactiva',
        ejecutando: 'ejecutando',
        completada: 'completada',
        detenida: 'detenida',
        limite_memoria: 'límite de memoria',
        limite_cpu: 'límite de CPU',
        error: 'error'
    };

    const TEXTO_CARGA = { memoria: 'memoria', sincronizacion: 'sincronización' };
    const TEXTO_MODELO = { nativos: 'nativos', tareas: 'tareas' };
    const TEXTO_SINCRONIZACION = { ninguna: 'ninguna', mutex: 'mutex', atomico: 'atómico' };

    function nombreEstado(estado) {
        return NOMBRE_ESTADO[estado] || String(estado);
    }

    function trabajoDe(resultado) {
        return resultado.config.carga === 'sincronizacion'
            ? resultado.contadorFinal
            : resultado.pasadasCompletadas;
    }

    function resumenConfig(config) {
        if (!config) return '';
        const unidades = config.modeloHilos === 'tareas' ? ' tareas' : ' hilos';
        if (config.carga === 'sincronizacion') {
            return config.hilos + unidades + ' · ' +
                (TEXTO_SINCRONIZACION[config.sincronizacion] || config.sincronizacion);
        }
        const partes = [config.hilos + unidades, config.memoriaMb + ' MB'];
        if (config.limiteMemoriaMb > 0) partes.push('límite ' + config.limiteMemoriaMb + ' MB');
        if (config.limiteCpuSegundos > 0) partes.push('CPU ' + config.limiteCpuSegundos + ' s');
        if (config.limiteCpuPorcentaje < 100) partes.push('CPU ≈ ' + config.limiteCpuPorcentaje + ' %');
        return partes.join(' · ');
    }

    const estado = {
        pestana: 'configurar',
        fuente: null,
        resultados: [],
        seleccionados: new Set(),
        escenarios: [],
        capacidades: [],
        sistema: null,
        corridaActiva: false,
        iniciando: false,
        cargaBloqueada: false,
        idActual: null,
        historico: null,
        limites: null,
        configCorrida: null,
        reinicioPendiente: false,
        ultimoEstado: null,
        orden: { campo: 'fecha', ascendente: false }
    };

    let graficoMemoria = null;
    let graficoCpu = null;
    let graficoFallos = null;
    let graficos = [];
    let dibujoPendiente = false;
    let temporizadorMensaje = null;

    async function fetchJson(url, opciones) {
        const configuracion = Object.assign({ cache: 'no-store' }, opciones || {});
        const respuesta = await fetch(url, configuracion);

        let datos = null;
        try {
            datos = await respuesta.json();
        } catch (error) {
            datos = null;
        }

        if (!respuesta.ok) {
            const mensaje = datos && datos.error ? datos.error : 'Error HTTP ' + respuesta.status;
            throw new Error(mensaje);
        }
        if (datos === null) throw new Error('La respuesta del servidor no es JSON válido.');
        return datos;
    }

    function mostrarMensaje(texto, tipo) {
        const elemento = $('mensaje');
        elemento.hidden = false;
        elemento.className = 'mensaje ' + (tipo || 'info');
        elemento.textContent = texto;

        if (temporizadorMensaje) clearTimeout(temporizadorMensaje);
        if (tipo !== 'error') {
            temporizadorMensaje = setTimeout(() => {
                elemento.hidden = true;
            }, 7000);
        }
    }

    function escalaBonita(maximo) {
        if (!Number.isFinite(maximo) || maximo <= 0) return 1;
        const exponente = Math.floor(Math.log10(maximo));
        const base = Math.pow(10, exponente);
        const normalizado = maximo / base;
        let paso = 10;
        if (normalizado <= 1) paso = 1;
        else if (normalizado <= 2) paso = 2;
        else if (normalizado <= 2.5) paso = 2.5;
        else if (normalizado <= 5) paso = 5;
        return paso * base;
    }

    class Grafico {
        constructor(canvas, series, opciones) {
            this.canvas = canvas;
            this.series = series;
            this.opciones = opciones || {};
            this.puntos = [];
            this.referencias = [];
            this.maximo = this.opciones.maximo || 720;
            this.sucio = true;
        }

        limpiar() {
            this.puntos = [];
            this.sucio = true;
        }

        agregar(t, valores) {
            this.puntos.push({ t: t, v: valores });
            if (this.puntos.length > this.maximo) this.puntos.shift();
            this.sucio = true;
        }

        dibujar() {
            const canvas = this.canvas;
            const dpr = window.devicePixelRatio || 1;
            const ancho = canvas.clientWidth;
            const alto = canvas.clientHeight;
            if (ancho < 40 || alto < 40) return false;

            const pixelesAncho = Math.round(ancho * dpr);
            const pixelesAlto = Math.round(alto * dpr);
            if (canvas.width !== pixelesAncho || canvas.height !== pixelesAlto) {
                canvas.width = pixelesAncho;
                canvas.height = pixelesAlto;
            }

            const ctx = canvas.getContext('2d');
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            ctx.clearRect(0, 0, ancho, alto);

            const margen = { izq: 64, der: 14, arriba: 24, abajo: 24 };
            const x0 = margen.izq;
            const y0 = margen.arriba;
            const x1 = ancho - margen.der;
            const y1 = alto - margen.abajo;
            const anchoTrazo = Math.max(1, x1 - x0);
            const altoTrazo = Math.max(1, y1 - y0);

            let maximo = 0;
            for (const punto of this.puntos) {
                for (const valor of punto.v) {
                    if (Number.isFinite(valor)) maximo = Math.max(maximo, valor);
                }
            }
            for (const referencia of this.referencias) {
                maximo = Math.max(maximo, referencia.valor);
            }
            if (maximo <= 0) maximo = this.opciones.minimo || 1;
            const tope = escalaBonita(maximo * 1.08);

            const tMax = this.puntos.length ? this.puntos[this.puntos.length - 1].t : 0;
            const xDe = (t) => (tMax > 0
                ? x0 + (Math.min(t, tMax) / tMax) * anchoTrazo
                : x0);
            const yDe = (v) => y1 - (Math.min(Math.max(v, 0), tope) / tope) * altoTrazo;

            ctx.font = '11px "Cascadia Mono", "JetBrains Mono", Consolas, monospace';
            ctx.textAlign = 'left';
            ctx.textBaseline = 'middle';
            let leyendaX = x0;
            for (const serie of this.series) {
                ctx.strokeStyle = serie.color;
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.moveTo(leyendaX, 10);
                ctx.lineTo(leyendaX + 12, 10);
                ctx.stroke();
                ctx.fillStyle = '#5f5e56';
                ctx.fillText(serie.nombre, leyendaX + 16, 10);
                leyendaX += 16 + ctx.measureText(serie.nombre).width + 14;
            }

            ctx.lineWidth = 1;
            ctx.textAlign = 'right';
            for (let indice = 0; indice <= 4; indice += 1) {
                const y = y1 - (altoTrazo * indice) / 4;
                ctx.strokeStyle = indice === 0 ? '#8d8a7e' : '#ded8c9';
                ctx.beginPath();
                ctx.moveTo(x0, y);
                ctx.lineTo(x1, y);
                ctx.stroke();
                ctx.fillStyle = '#5f5e56';
                ctx.fillText(this.opciones.formatearEje((tope * indice) / 4), x0 - 8, y);
            }

            ctx.textAlign = 'center';
            ctx.textBaseline = 'top';
            for (let indice = 0; indice < 3; indice += 1) {
                const t = (tMax * indice) / 2;
                const x = x0 + (anchoTrazo * indice) / 2;
                ctx.fillStyle = '#5f5e56';
                ctx.fillText(numero(t, tMax < 10 ? 1 : 0) + ' s', x, y1 + 7);
            }

            for (const referencia of this.referencias) {
                if (referencia.valor <= 0 || referencia.valor > tope) continue;
                const y = yDe(referencia.valor);
                ctx.save();
                ctx.setLineDash([4, 4]);
                ctx.strokeStyle = referencia.color;
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.moveTo(x0, y);
                ctx.lineTo(x1, y);
                ctx.stroke();
                ctx.restore();
                ctx.fillStyle = referencia.color;
                ctx.textAlign = 'left';
                ctx.textBaseline = 'bottom';
                ctx.fillText(referencia.etiqueta, x0 + 4, y - 2);
            }

            if (!this.puntos.length) {
                ctx.fillStyle = '#706d64';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText('Sin datos todavía', (x0 + x1) / 2, (y0 + y1) / 2);
                return true;
            }

            ctx.save();
            ctx.beginPath();
            ctx.rect(x0, y0, anchoTrazo, altoTrazo);
            ctx.clip();

            for (let indice = 0; indice < this.series.length; indice += 1) {
                ctx.strokeStyle = this.series[indice].color;
                ctx.lineWidth = 1.8;
                ctx.lineJoin = 'round';
                ctx.beginPath();
                let empezado = false;
                for (const punto of this.puntos) {
                    const valor = punto.v[indice];
                    if (!Number.isFinite(valor)) {
                        empezado = false;
                        continue;
                    }
                    const x = xDe(punto.t);
                    const y = yDe(valor);
                    if (!empezado) {
                        ctx.moveTo(x, y);
                        empezado = true;
                    } else {
                        ctx.lineTo(x, y);
                    }
                }
                ctx.stroke();
            }

            const ultimo = this.puntos[this.puntos.length - 1];
            for (let indice = 0; indice < this.series.length; indice += 1) {
                const valor = ultimo.v[indice];
                if (!Number.isFinite(valor)) continue;
                ctx.fillStyle = this.series[indice].color;
                ctx.beginPath();
                ctx.arc(xDe(ultimo.t), yDe(valor), 2.4, 0, Math.PI * 2);
                ctx.fill();
            }
            ctx.restore();
            return true;
        }
    }

    function solicitarDibujo() {
        if (dibujoPendiente) return;
        dibujoPendiente = true;
        requestAnimationFrame(() => {
            dibujoPendiente = false;
            for (const grafico of graficos) {
                if (grafico.sucio) grafico.sucio = !grafico.dibujar();
            }
        });
    }

    function crearGraficos() {
        graficoMemoria = new Grafico($('graficoMemoria'), [
            { nombre: 'VA', color: '#1f4f8f' },
            { nombre: 'RSS', color: '#1d6b4a' },
            { nombre: 'swap del proceso', color: '#a3231b' }
        ], { formatearEje: formatearEjeKb, minimo: 1024 });

        graficoCpu = new Grafico($('graficoCpu'), [
            { nombre: 'CPU', color: '#8a5a00' }
        ], { formatearEje: (v) => numero(v, 0) + ' %', minimo: 100 });
        graficoCpu.referencias = [{ valor: 100, etiqueta: '100 %', color: '#8d8a7e' }];

        graficoFallos = new Grafico($('graficoFallos'), [
            { nombre: 'menores', color: '#14606b' },
            { nombre: 'mayores', color: '#a34a00' }
        ], { formatearEje: formatearEjeNumero, minimo: 1 });

        graficos = [graficoMemoria, graficoCpu, graficoFallos];
        window.addEventListener('resize', () => {
            for (const grafico of graficos) grafico.sucio = true;
            solicitarDibujo();
        });
        solicitarDibujo();
    }

    function limpiarGraficos() {
        for (const grafico of graficos) grafico.limpiar();
        solicitarDibujo();
    }

    function agregarMuestraAGraficos(muestra) {
        const t = muestra.msDesdeInicio / 1000;
        graficoMemoria.agregar(t, [muestra.vmSizeKb, muestra.rssKb, muestra.vmSwapKb]);
        graficoCpu.agregar(t, [muestra.cpuPorcentaje]);
        graficoFallos.agregar(t, [muestra.fallosMenores, muestra.fallosMayores]);
        solicitarDibujo();
    }

    function cambiarPestana(nombre) {
        estado.pestana = nombre;

        document.querySelectorAll('.pestana').forEach((boton) => {
            boton.classList.toggle('activa', boton.dataset.pestana === nombre);
        });
        document.querySelectorAll('.vista').forEach((vista) => {
            vista.classList.toggle('activa', vista.id === 'vista-' + nombre);
        });

        if (nombre === 'vivo') {
            for (const grafico of graficos) grafico.sucio = true;
            solicitarDibujo();
        }
        if (nombre === 'resultados') cargarResultados();
        if (nombre === 'capacidades' && !estado.capacidades.length) cargarCapacidades();
    }

    function configurarPestanas() {
        document.querySelectorAll('.pestana').forEach((boton) => {
            boton.addEventListener('click', () => cambiarPestana(boton.dataset.pestana));
        });
    }

    const CAMPOS_NUMERICOS = [
        'inpHilos', 'inpMemoria', 'inpPasadas', 'inpDuracion', 'inpIteraciones',
        'inpLimiteMemoria', 'inpLimiteCpuSegundos', 'inpLimiteCpuPorcentaje'
    ];

    function numeroDe(id, porDefecto) {
        const valor = Number($(id).value);
        return Number.isFinite(valor) ? valor : porDefecto;
    }

    function actualizarCamposPorCarga() {
        const carga = $('selCarga').value;

        document.querySelectorAll('[data-solo]').forEach((contenedor) => {
            const aplica = contenedor.dataset.solo === carga;
            contenedor.classList.toggle('deshabilitado', !aplica);
            contenedor.querySelectorAll('input, select').forEach((control) => {
                control.disabled = !aplica;
            });
        });

        const usaDuracion = carga === 'memoria' && $('selModoFin').value === 'duracion';
        const campoDuracion = $('campoDuracion');
        campoDuracion.classList.toggle('deshabilitado', !usaDuracion);
        $('inpDuracion').disabled = !usaDuracion;
    }

    function bloquearCarga(bloquear) {
        estado.cargaBloqueada = bloquear;
        $('selCarga').disabled = bloquear;
        $('campoCarga').classList.toggle('deshabilitado', bloquear);
        const ayuda = $('ayudaCarga');
        if (ayuda) ayuda.hidden = !bloquear;
    }

    function actualizarReferenciaCpu() {
        if (!graficoCpu) return;
        const porcentaje = numeroDe('inpLimiteCpuPorcentaje', 100);
        const referencias = [{ valor: 100, etiqueta: '100 %', color: '#8d8a7e' }];
        if (porcentaje > 0 && porcentaje < 100) {
            referencias.push({
                valor: porcentaje,
                etiqueta: 'límite ≈ ' + numero(porcentaje, 0) + ' %',
                color: '#a3231b'
            });
        }
        graficoCpu.referencias = referencias;
        graficoCpu.sucio = true;
        solicitarDibujo();
    }

    function aplicarConfig(config) {
        $('selCarga').value = config.carga;
        $('selModelo').value = config.modeloHilos;
        $('inpHilos').value = config.hilos;
        $('inpMemoria').value = config.memoriaMb;
        $('inpPasadas').value = config.pasadas;
        $('selModoFin').value = config.modoFin;
        $('inpDuracion').value = config.duracionSeg;
        $('selSincronizacion').value = config.sincronizacion;
        $('inpIteraciones').value = config.iteraciones;
        $('inpLimiteMemoria').value = config.limiteMemoriaMb;
        $('inpLimiteCpuSegundos').value = config.limiteCpuSegundos;
        $('inpLimiteCpuPorcentaje').value = config.limiteCpuPorcentaje;

        actualizarCamposPorCarga();
        actualizarReferenciaCpu();
    }

    function renderVariantes(escenario) {
        const contenedor = $('variantesEscenario');
        contenedor.innerHTML = '';

        if (!escenario || !escenario.variantes || escenario.variantes.length < 2) {
            contenedor.hidden = true;
            return;
        }

        for (const variante of escenario.variantes) {
            const boton = document.createElement('button');
            boton.type = 'button';
            boton.className = 'variante';
            boton.textContent = variante.etiqueta;
            boton.title = 'Cargar esta variante en el formulario';
            boton.addEventListener('click', () => {
                aplicarConfig(variante.config);
                bloquearCarga(true);
                contenedor.querySelectorAll('.variante').forEach((otro) => {
                    otro.classList.toggle('elegida', otro === boton);
                });
            });
            contenedor.appendChild(boton);
        }
        contenedor.hidden = false;
    }

    function aplicarEscenario(id) {
        const nota = $('notaEscenario');
        const escenario = estado.escenarios.find((candidato) => candidato.id === id);

        if (!escenario) {
            nota.hidden = true;
            nota.innerHTML = '';
            renderVariantes(null);
            bloquearCarga(false);
            return;
        }

        aplicarConfig(escenario.base);
        bloquearCarga(true);

        const pasos = escenario.pasos
            .map((paso) => '<li>' + escaparHtml(paso) + '</li>')
            .join('');
        nota.innerHTML = '<strong>' + escaparHtml(escenario.nombre) + '</strong> — ' +
            escaparHtml(escenario.descripcion) +
            '<ol>' + pasos + '</ol>';
        nota.hidden = false;

        renderVariantes(escenario);
    }

    function leerConfiguracion() {
        for (const id of CAMPOS_NUMERICOS) {
            const control = $(id);
            if (!control.disabled && !control.checkValidity()) {
                control.reportValidity();
                return null;
            }
        }

        const carga = $('selCarga').value;
        const modoFin = $('selModoFin').value;
        const limiteMemoria = numeroDe('inpLimiteMemoria', 0);

        if (limiteMemoria !== 0 && (limiteMemoria < 8 || limiteMemoria > 4096)) {
            mostrarMensaje('El límite de memoria debe ser 0 (sin límite) u 8..4096 MB.', 'aviso');
            return null;
        }

        return {
            carga: carga,
            modeloHilos: $('selModelo').value,
            hilos: numeroDe('inpHilos', 1),
            memoriaMb: carga === 'memoria' ? numeroDe('inpMemoria', 128) : 128,
            pasadas: carga === 'memoria' ? numeroDe('inpPasadas', 20) : 20,
            modoFin: carga === 'memoria' ? modoFin : 'pasadas',
            duracionSeg: (carga === 'memoria' && modoFin === 'duracion')
                ? numeroDe('inpDuracion', 5)
                : 5,
            sincronizacion: carga === 'sincronizacion'
                ? $('selSincronizacion').value
                : 'ninguna',
            iteraciones: carga === 'sincronizacion'
                ? numeroDe('inpIteraciones', 1000000)
                : 1000000,
            limiteMemoriaMb: limiteMemoria,
            limiteCpuSegundos: numeroDe('inpLimiteCpuSegundos', 0),
            limiteCpuPorcentaje: numeroDe('inpLimiteCpuPorcentaje', 100)
        };
    }

    function configurarFormulario() {
        $('selCarga').addEventListener('change', actualizarCamposPorCarga);
        $('selModoFin').addEventListener('change', actualizarCamposPorCarga);
        $('selEscenario').addEventListener('change', (evento) => {
            aplicarEscenario(evento.target.value);
        });
        $('inpLimiteCpuPorcentaje').addEventListener('input', actualizarReferenciaCpu);

        actualizarCamposPorCarga();
        actualizarReferenciaCpu();
        bloquearCarga(false);
    }

    function actualizarBotones() {
        const activa = estado.corridaActiva;
        const ocupado = activa || estado.iniciando;
        const historico = !!estado.historico;

        $('btnIniciar').disabled = ocupado;
        $('btnDetener').disabled = !activa;
        $('btnRepetir').disabled = ocupado || !estado.configCorrida;
        $('btnComparar').disabled = estado.seleccionados.size < 2;

        const barra = $('accionesVivo');
        if (!barra) return;
        barra.hidden = historico;

        const puedeRepetir = !ocupado && !!estado.configCorrida;
        $('btnVivoIniciar').disabled = ocupado || historico;
        $('btnVivoDetener').disabled = !activa || historico;
        $('btnVivoRepetir').disabled = !puedeRepetir || historico;
        $('btnVivoReiniciar').disabled =
            (!activa && !puedeRepetir) || estado.iniciando ||
            estado.reinicioPendiente || historico;

        const insignia = $('badgeEstadoVivo');
        if (insignia) {
            if (historico) {
                insignia.textContent = nombreEstado(estado.historico.estado);
                insignia.className = 'estado-corrida ' + estado.historico.estado;
            } else if (activa) {
                insignia.textContent = 'ejecutando';
                insignia.className = 'estado-corrida ejecutando';
            } else if (estado.iniciando) {
                insignia.textContent = 'iniciando';
                insignia.className = 'estado-corrida ejecutando';
            } else if (estado.ultimoEstado) {
                insignia.textContent = nombreEstado(estado.ultimoEstado);
                insignia.className = 'estado-corrida ' + estado.ultimoEstado;
            } else {
                insignia.textContent = 'inactiva';
                insignia.className = 'estado-corrida';
            }
        }
    }

    function limpiarTarjetas() {
        ['cardDuracion', 'cardVa', 'cardRss', 'cardSwapProc', 'cardSwapSistema',
            'cardFallos', 'cardCpu', 'cardHilos'].forEach((id) => {
            $(id).textContent = '—';
        });
        $('cardEstado').textContent = 'inactiva';
        $('barraProgreso').style.width = '0%';
        $('textoProgreso').textContent = '0 %';
    }

    function actualizarProgreso(progreso, ms) {
        const fraccion = Math.max(0, Math.min(1, Number(progreso) || 0));
        $('barraProgreso').style.width = (fraccion * 100).toFixed(1) + '%';
        $('textoProgreso').textContent = numero(fraccion * 100, 0) + ' %';
        if (Number.isFinite(ms)) $('cardDuracion').textContent = formatearDuracion(ms);
    }

    function textoSwapSistema(totalKb, libreKb) {
        if (!totalKb) return 'sin swap';
        return formatearKb(totalKb - libreKb) + ' de ' + formatearKb(totalKb);
    }

    function actualizarTarjetas(muestra) {
        $('cardVa').textContent = formatearKb(muestra.vmSizeKb);
        $('cardRss').textContent = formatearKb(muestra.rssKb);
        $('cardSwapProc').textContent = formatearKb(muestra.vmSwapKb);
        $('cardSwapSistema').textContent =
            textoSwapSistema(muestra.swapTotalKb, muestra.swapLibreKb);
        $('cardFallos').textContent =
            numero(muestra.fallosMenores, 0) + ' / ' + numero(muestra.fallosMayores, 0);
        $('cardFallos').title = numero(muestra.fallosPorSegundo, 0) + ' fallos por segundo';
        $('cardCpu').textContent = formatearPorcentaje(muestra.cpuPorcentaje);
        $('cardHilos').textContent = numero(muestra.hilos, 0);
        $('cardDuracion').textContent = formatearDuracion(muestra.msDesdeInicio);
    }

    function actualizarTarjetasFinales(resultado) {
        $('cardVa').textContent = formatearKb(resultado.vmPicoKb);
        $('cardRss').textContent = formatearKb(resultado.rssPicoKb);
        $('cardSwapProc').textContent = formatearKb(resultado.swapPicoKb);
        $('cardSwapSistema').textContent =
            textoSwapSistema(resultado.swapTotalKb, resultado.swapLibreKb);
        $('cardFallos').textContent =
            numero(resultado.fallosMenores, 0) + ' / ' + numero(resultado.fallosMayores, 0);
        $('cardFallos').title = '';
        $('cardCpu').textContent = formatearPorcentaje(resultado.cpuPorcentajePromedio);
        $('cardDuracion').textContent = formatearDuracion(resultado.duracionMs);
        $('cardEstado').textContent = nombreEstado(resultado.estado);

        const ultima = (resultado.muestras && resultado.muestras.length)
            ? resultado.muestras[resultado.muestras.length - 1]
            : null;
        if (ultima) {
            $('cardHilos').textContent = numero(ultima.hilos, 0);
        } else if (resultado.config) {
            const hilosSo = resultado.config.modeloHilos === 'tareas'
                ? 1
                : resultado.config.hilos;
            $('cardHilos').textContent = numero(hilosSo, 0);
        }

        if (resultado.estado === 'completada') actualizarProgreso(1, resultado.duracionMs);
    }

    function textoLimites(resultado) {
        const cuota = resultado.config
            ? resultado.config.limiteCpuPorcentaje
            : resultado.limiteCpuPorcentaje;
        if (resultado.limitesVerificados) {
            const tieneCuota = cuota > 0 && cuota < 100;
            return tieneCuota ? '✓ RLIMIT · cuota ≈' : '✓ verificados';
        }
        if (resultado.limiteCpuPorcentajeAplicado) return '≈ cuota aproximada';
        return '—';
    }

    function actualizarBadgeLimites(config, resultado) {
        const insignia = $('badgeLimites');
        insignia.className = 'insignia';

        if (!config) {
            insignia.textContent = 'límites: sin pedir';
            return;
        }

        const tieneRlimit = config.limiteMemoriaMb > 0 || config.limiteCpuSegundos > 0;
        const tieneCuota = config.limiteCpuPorcentaje > 0 && config.limiteCpuPorcentaje < 100;

        const partes = [];
        if (config.limiteMemoriaMb > 0) {
            partes.push('RLIMIT_AS ' + numero(config.limiteMemoriaMb, 0) + ' MB');
        }
        if (config.limiteCpuSegundos > 0) {
            partes.push('RLIMIT_CPU ' + numero(config.limiteCpuSegundos, 0) + ' s');
        }
        if (tieneCuota) {
            partes.push('CPU ≈ ' + numero(config.limiteCpuPorcentaje, 0) + ' % (aprox.)');
        }

        if (!partes.length) {
            insignia.textContent = 'límites: sin pedir';
            return;
        }

        let sufijo = '';
        if (resultado) {
            if (resultado.limitesVerificados) {
                sufijo = tieneCuota ? ' · RLIMIT ✓ · cuota aprox.' : ' ✓ verificados';
                insignia.className = 'insignia verde';
            } else if (!tieneRlimit && tieneCuota && resultado.limiteCpuPorcentajeAplicado) {
                sufijo = ' · aplicado (aproximado)';
                insignia.className = 'insignia ambar';
            } else {
                sufijo = ' · sin verificar';
                insignia.className = 'insignia rojo';
            }
        } else {
            sufijo = tieneRlimit ? ' · verificando' : ' · aplicando';
            insignia.className = 'insignia ambar';
        }

        insignia.textContent = partes.join(' · ') + sufijo;
    }

    async function lanzarCorrida(config) {
        if (estado.corridaActiva || estado.iniciando) return false;
        if (!config) return false;

        estado.iniciando = true;
        actualizarBotones();

        try {
            const respuesta = await fetchJson('/api/corrida/iniciar', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(config)
            });

            estado.historico = null;
            $('notaHistorico').hidden = true;
            estado.corridaActiva = true;
            estado.idActual = respuesta.id;
            estado.limites = config;
            estado.configCorrida = Object.assign({}, config);
            estado.ultimoEstado = 'ejecutando';

            limpiarGraficos();
            limpiarTarjetas();
            $('cardEstado').textContent = 'ejecutando';
            $('badgeId').textContent = respuesta.id;
            $('badgeEstado').textContent = 'ejecutando';
            $('badgeEstado').className = 'estado-corrida ejecutando';
            actualizarBadgeLimites(config, null);
            actualizarReferenciaCpu();

            abrirFuente();
            cambiarPestana('vivo');
            mostrarMensaje('Corrida ' + respuesta.id + ' iniciada.', 'info');
            return true;
        } catch (error) {
            mostrarMensaje('No se pudo iniciar la corrida: ' + error.message, 'error');
            return false;
        } finally {
            estado.iniciando = false;
            actualizarBotones();
        }
    }

    async function iniciarCorrida() {
        if (estado.corridaActiva || estado.iniciando) return;

        const config = leerConfiguracion();
        if (!config) return;

        await lanzarCorrida(config);
    }

    async function repetirCorrida() {
        if (!estado.configCorrida) return;
        await lanzarCorrida(estado.configCorrida);
    }

    async function reiniciarCorrida() {
        if (estado.iniciando || estado.reinicioPendiente) return;
        if (!estado.configCorrida) return;

        if (!estado.corridaActiva) {
            await lanzarCorrida(estado.configCorrida);
            return;
        }

        estado.reinicioPendiente = true;
        actualizarBotones();
        mostrarMensaje(
            'Reiniciando: se detiene la corrida actual y se lanza otra con la misma configuración.',
            'aviso');
        await detenerCorrida();
    }

    async function detenerCorrida() {
        try {
            await fetchJson('/api/corrida/detener', { method: 'POST' });
            mostrarMensaje(
                'Detención solicitada: se envía SIGTERM al trabajador y, si no responde en 2 s, SIGKILL.',
                'aviso');
        } catch (error) {
            estado.reinicioPendiente = false;
            mostrarMensaje('No se pudo detener la corrida: ' + error.message, 'error');
        } finally {
            actualizarBotones();
        }
    }

    function exportarCsv() {
        const ids = Array.from(estado.seleccionados);
        let url = '/api/resultados/exportar.csv';
        if (ids.length) url += '?ids=' + encodeURIComponent(ids.join(','));

        const enlace = document.createElement('a');
        enlace.href = url;
        enlace.download = 'resultados.csv';
        document.body.appendChild(enlace);
        enlace.click();
        enlace.remove();
    }

    function configurarBotones() {
        $('btnIniciar').addEventListener('click', iniciarCorrida);
        $('btnRepetir').addEventListener('click', repetirCorrida);
        $('btnDetener').addEventListener('click', detenerCorrida);
        $('btnVivoIniciar').addEventListener('click', iniciarCorrida);
        $('btnVivoDetener').addEventListener('click', detenerCorrida);
        $('btnVivoRepetir').addEventListener('click', repetirCorrida);
        $('btnVivoReiniciar').addEventListener('click', reiniciarCorrida);
        $('btnVivoConfigurar').addEventListener('click', () => cambiarPestana('configurar'));
        $('btnVivoResultados').addEventListener('click', () => cambiarPestana('resultados'));
        $('btnRefrescarResultados').addEventListener('click', () => cargarResultados());
        $('btnComparar').addEventListener('click', compararSeleccionadas);
        $('btnCsv').addEventListener('click', exportarCsv);
        $('btnRefrescarCapacidades').addEventListener('click', () => cargarCapacidades());
    }

    function abrirFuente() {
        if (estado.fuente) return;
        if (typeof EventSource === 'undefined') {
            mostrarMensaje(
                'Este navegador no soporta EventSource: la vista en vivo no se actualizará sola.',
                'error');
            return;
        }

        const fuente = new EventSource('/api/eventos');

        fuente.addEventListener('estado', (evento) => {
            try {
                aplicarEstado(JSON.parse(evento.data));
            } catch (error) {
            }
        });

        fuente.addEventListener('muestra', (evento) => {
            try {
                aplicarMuestra(JSON.parse(evento.data));
            } catch (error) {
            }
        });

        fuente.addEventListener('fin', (evento) => {
            try {
                aplicarFin(JSON.parse(evento.data));
            } catch (error) {
            }
        });

        fuente.onerror = () => {};
        estado.fuente = fuente;
    }

    function aplicarEstado(nuevo) {
        if (!nuevo || typeof nuevo !== 'object') return;

        if (!nuevo.activo) {
            if (nuevo.estado) estado.ultimoEstado = nuevo.estado;
            if (estado.corridaActiva && !estado.historico) {
                estado.corridaActiva = false;
                actualizarBotones();
            }
            return;
        }

        if (estado.historico) {
            estado.historico = null;
            $('notaHistorico').hidden = true;
            limpiarGraficos();
        }

        estado.corridaActiva = true;
        estado.idActual = nuevo.id;
        estado.limites = nuevo.config;
        estado.ultimoEstado = 'ejecutando';
        if (nuevo.config) estado.configCorrida = Object.assign({}, nuevo.config);

        $('badgeId').textContent = nuevo.id || 'sin corrida';
        $('badgeEstado').textContent = 'ejecutando';
        $('badgeEstado').className = 'estado-corrida ejecutando';
        $('cardEstado').textContent = 'ejecutando';
        actualizarBadgeLimites(nuevo.config, null);
        actualizarProgreso(nuevo.progreso, nuevo.msDesdeInicio);
        actualizarBotones();

        if (nuevo.muestra) aplicarMuestra(nuevo.muestra);
    }

    function aplicarMuestra(muestra) {
        if (!muestra || typeof muestra !== 'object') return;
        if (estado.historico) return;

        agregarMuestraAGraficos(muestra);
        actualizarTarjetas(muestra);
        actualizarProgreso(muestra.progreso, muestra.msDesdeInicio);
    }

    function aplicarFin(datos) {
        if (!datos || typeof datos !== 'object') return;

        const idCerrada = datos.id;
        const estabaPendiente = estado.reinicioPendiente;
        estado.reinicioPendiente = false;
        estado.corridaActiva = false;
        estado.idActual = datos.id || estado.idActual;

        const estadoFinal = datos.estado || 'error';
        estado.ultimoEstado = estadoFinal;
        $('badgeEstado').textContent = nombreEstado(estadoFinal);
        $('badgeEstado').className = 'estado-corrida ' + estadoFinal;
        $('cardEstado').textContent = nombreEstado(estadoFinal);
        actualizarBotones();

        const tipo = estadoFinal === 'completada' ? 'exito'
            : (estadoFinal === 'error' ? 'error' : 'aviso');
        mostrarMensaje('Corrida ' + datos.id + ' terminada: ' + nombreEstado(estadoFinal) + '.', tipo);

        cargarResultados();

        fetchJson('/api/resultados/' + encodeURIComponent(datos.id))
            .then((resultado) => {
                if (estado.historico) return;
                if (estado.idActual !== idCerrada) return;
                actualizarTarjetasFinales(resultado);
                actualizarBadgeLimites(resultado.config, resultado);
            })
            .catch(() => {
            });

        if (estabaPendiente && estado.configCorrida) {
            lanzarCorrida(estado.configCorrida);
        }
    }

    async function cargarResultados() {
        try {
            const datos = await fetchJson('/api/resultados');
            estado.resultados = Array.isArray(datos.resultados) ? datos.resultados : [];

            const ids = new Set(estado.resultados.map((resultado) => resultado.id));
            for (const id of Array.from(estado.seleccionados)) {
                if (!ids.has(id)) estado.seleccionados.delete(id);
            }

            renderResultados();
        } catch (error) {
            mostrarMensaje('No se pudo leer el historial: ' + error.message, 'error');
        }
    }

    function valorOrden(resultado, campo) {
        switch (campo) {
            case 'fecha': return resultado.fecha || '';
            case 'estado': return resultado.estado || '';
            case 'carga': return resultado.carga || '';
            case 'modeloHilos': return resultado.modeloHilos || '';
            case 'limitesVerificados': return resultado.limitesVerificados ? 1 : 0;
            default: {
                const valor = Number(resultado[campo]);
                return Number.isFinite(valor) ? valor : 0;
            }
        }
    }

    function renderResultados() {
        const cuerpo = $('cuerpoResultados');
        const lista = estado.resultados.slice();

        lista.sort((a, b) => {
            const va = valorOrden(a, estado.orden.campo);
            const vb = valorOrden(b, estado.orden.campo);
            let comparacion = 0;
            if (typeof va === 'string' || typeof vb === 'string') {
                comparacion = String(va).localeCompare(String(vb));
            } else {
                comparacion = va - vb;
            }
            return estado.orden.ascendente ? comparacion : -comparacion;
        });

        cuerpo.innerHTML = lista.map((resultado) => {
            const seleccionada = estado.seleccionados.has(resultado.id);
            return '<tr data-id="' + escaparHtml(resultado.id) + '"' +
                (seleccionada ? ' class="seleccionada"' : '') + '>' +
                '<td><input type="checkbox" data-id="' + escaparHtml(resultado.id) + '"' +
                (seleccionada ? ' checked' : '') +
                ' aria-label="Seleccionar ' + escaparHtml(resultado.id) + '"></td>' +
                '<td><div class="id-celda"><code>' + escaparHtml(resultado.id) + '</code>' +
                '<button type="button" class="boton-mini" data-ver="' +
                escaparHtml(resultado.id) + '">Ver</button></div></td>' +
                '<td>' + formatearFecha(resultado.fecha) + '</td>' +
                '<td><span class="pastilla ' + escaparHtml(resultado.estado) + '">' +
                escaparHtml(nombreEstado(resultado.estado)) + '</span></td>' +
                '<td>' + escaparHtml(TEXTO_CARGA[resultado.carga] || resultado.carga) + '</td>' +
                '<td>' + escaparHtml(TEXTO_MODELO[resultado.modeloHilos] || resultado.modeloHilos) + '</td>' +
                '<td class="numero">' + numero(resultado.hilos, 0) + '</td>' +
                '<td class="numero">' + formatearDuracion(resultado.duracionMs) + '</td>' +
                '<td class="numero">' + formatearKb(resultado.rssPicoKb) + '</td>' +
                '<td class="numero">' + formatearKb(resultado.vmPicoKb) + '</td>' +
                '<td class="numero">' + numero(resultado.fallosMenores, 0) + ' / ' +
                numero(resultado.fallosMayores, 0) + '</td>' +
                '<td class="numero">' + formatearPorcentaje(resultado.cpuPorcentajePromedio) + '</td>' +
                '<td>' + escaparHtml(textoLimites(resultado)) + '</td>' +
                '</tr>';
        }).join('');

        const cantidad = lista.length;
        const seleccionadas = estado.seleccionados.size;
        $('pistaResultados').textContent = cantidad
            ? cantidad + (cantidad === 1 ? ' corrida guardada' : ' corridas guardadas') +
              ' · ' + seleccionadas + ' seleccionada' + (seleccionadas === 1 ? '' : 's') +
              ' · seleccioná dos o más para comparar.'
            : 'Todavía no hay corridas guardadas.';

        document.querySelectorAll('#tablaResultados th[data-orden]').forEach((th) => {
            th.classList.remove('ordenado-asc', 'ordenado-desc');
            if (th.dataset.orden === estado.orden.campo) {
                th.classList.add(estado.orden.ascendente ? 'ordenado-asc' : 'ordenado-desc');
            }
        });

        actualizarBotones();
    }

    function configurarResultados() {
        $('cuerpoResultados').addEventListener('change', (evento) => {
            const casilla = evento.target.closest('input[type="checkbox"][data-id]');
            if (!casilla) return;

            const id = casilla.dataset.id;
            if (casilla.checked) estado.seleccionados.add(id);
            else estado.seleccionados.delete(id);

            const fila = casilla.closest('tr');
            if (fila) fila.classList.toggle('seleccionada', casilla.checked);

            const seleccionadas = estado.seleccionados.size;
            $('pistaResultados').textContent =
                estado.resultados.length + (estado.resultados.length === 1
                    ? ' corrida guardada'
                    : ' corridas guardadas') +
                ' · ' + seleccionadas + ' seleccionada' + (seleccionadas === 1 ? '' : 's') +
                ' · seleccioná dos o más para comparar.';
            actualizarBotones();
        });

        $('cuerpoResultados').addEventListener('click', (evento) => {
            const boton = evento.target.closest('button[data-ver]');
            if (!boton) return;
            verResultado(boton.dataset.ver);
        });

        document.querySelectorAll('#tablaResultados th[data-orden]').forEach((th) => {
            th.addEventListener('click', () => {
                const campo = th.dataset.orden;
                if (estado.orden.campo === campo) {
                    estado.orden.ascendente = !estado.orden.ascendente;
                } else {
                    estado.orden = {
                        campo: campo,
                        ascendente: campo !== 'fecha'
                    };
                }
                renderResultados();
            });
        });
    }

    async function verResultado(id) {
        try {
            const resultado = await fetchJson('/api/resultados/' + encodeURIComponent(id));
            estado.historico = resultado;

            limpiarGraficos();
            const muestras = Array.isArray(resultado.muestras) ? resultado.muestras : [];
            for (const muestra of muestras) agregarMuestraAGraficos(muestra);

            actualizarTarjetasFinales(resultado);
            actualizarBadgeLimites(resultado.config, resultado);
            $('badgeId').textContent = resultado.id;
            $('badgeEstado').textContent = nombreEstado(resultado.estado);
            $('badgeEstado').className = 'estado-corrida ' + resultado.estado;
            actualizarBotones();

            const nota = $('notaHistorico');
            nota.innerHTML = 'Mostrando la corrida histórica <code>' +
                escaparHtml(resultado.id) + '</code> (' + formatearFecha(resultado.fecha) + ')' +
                (muestras.length ? '' : ' · sin muestras: la corrida fue demasiado corta') +
                ' <button type="button" class="boton" id="btnVolverVivo">Volver al vivo</button>';
            nota.hidden = false;
            $('btnVolverVivo').addEventListener('click', volverAlVivo);

            cambiarPestana('vivo');
        } catch (error) {
            mostrarMensaje('No se pudo abrir la corrida ' + id + ': ' + error.message, 'error');
        }
    }

    function volverAlVivo() {
        estado.historico = null;
        $('notaHistorico').hidden = true;
        limpiarGraficos();
        limpiarTarjetas();

        if (estado.corridaActiva) {
            $('badgeEstado').textContent = 'ejecutando';
            $('badgeEstado').className = 'estado-corrida ejecutando';
            $('cardEstado').textContent = 'ejecutando';
            estado.ultimoEstado = 'ejecutando';
        } else {
            $('badgeId').textContent = 'sin corrida';
            $('badgeEstado').textContent = 'inactiva';
            $('badgeEstado').className = 'estado-corrida';
            estado.ultimoEstado = null;
            actualizarBadgeLimites(null, null);
        }
        actualizarBotones();
    }

    function motivosNoComparables(corridas) {
        const motivos = [];
        const unico = (obtener) => new Set(corridas.map(obtener)).size === 1;

        if (!unico((r) => r.config.carga)) motivos.push('distinta carga');
        if (!unico((r) => r.config.modeloHilos)) motivos.push('distinto modelo de hilos');
        if (!unico((r) => r.config.limiteMemoriaMb)) motivos.push('distinto límite de memoria');

        if (corridas[0].config.carga === 'memoria') {
            if (!unico((r) => r.config.memoriaMb)) motivos.push('distinta cantidad de memoria');
            if (!unico((r) => r.config.pasadas)) motivos.push('distinta cantidad de pasadas');
            if (!unico((r) => r.config.modoFin)) motivos.push('distinto modo de fin');
        }
        return motivos;
    }

    function claseMejora(mejor, delta) {
        if (!mejor || delta === 0) return '';
        if (mejor === 'menor') return delta < 0 ? 'positivo' : 'negativo';
        if (mejor === 'mayor') return delta > 0 ? 'positivo' : 'negativo';
        return '';
    }

    function formatearValor(tipo, valor) {
        switch (tipo) {
            case 'ms': return formatearDuracion(valor);
            case 'kb': return formatearKb(valor);
            case 'pct': return formatearPorcentaje(valor);
            case 'num2': return numero(valor, 2);
            default: return numero(valor, 0);
        }
    }

    async function compararSeleccionadas() {
        const ids = Array.from(estado.seleccionados);
        if (ids.length < 2) {
            mostrarMensaje('Seleccioná al menos dos corridas en la pestaña Resultados.', 'aviso');
            return;
        }

        try {
            const corridas = await Promise.all(
                ids.map((id) => fetchJson('/api/resultados/' + encodeURIComponent(id))));
            corridas.sort((a, b) => {
                const porHilos = a.config.hilos - b.config.hilos;
                if (porHilos !== 0) return porHilos;
                return String(a.fecha).localeCompare(String(b.fecha));
            });

            renderComparacion(corridas);
            cambiarPestana('comparar');
        } catch (error) {
            mostrarMensaje('No se pudo comparar: ' + error.message, 'error');
        }
    }

    function renderComparacion(corridas) {
        const aviso = $('avisoComparacion');
        const motivos = motivosNoComparables(corridas);

        if (motivos.length) {
            aviso.hidden = false;
            aviso.innerHTML = '<strong>Comparación no estricta.</strong> Las corridas difieren en ' +
                escaparHtml(motivos.join(', ')) +
                '. Se muestran los valores, pero el speedup no se calcula.';
        } else {
            aviso.hidden = true;
            aviso.innerHTML = '';
        }

        const columnas = corridas.length + 1;
        let cabecera = '<th>Métrica</th>';
        for (const corrida of corridas) {
            cabecera += '<th>' + escaparHtml(corrida.id) + '<br><small>' +
                escaparHtml(resumenConfig(corrida.config)) + '</small></th>';
        }
        $('cabeceraComparacion').innerHTML = cabecera;

        const filas = [
            { nombre: 'Estado', valor: (r) => nombreEstado(r.estado), texto: true },
            { nombre: 'Carga', valor: (r) => TEXTO_CARGA[r.config.carga] || r.config.carga, texto: true },
            { nombre: 'Modelo de hilos', valor: (r) => TEXTO_MODELO[r.config.modeloHilos] || r.config.modeloHilos, texto: true },
            { nombre: 'Hilos/tareas', valor: (r) => r.config.hilos, tipo: 'num' },
            { nombre: 'Duración', valor: (r) => r.duracionMs, tipo: 'ms' },
            { nombre: 'Memoria virtual pico (VA)', valor: (r) => r.vmPicoKb, tipo: 'kb' },
            { nombre: 'RAM residente pico (RSS)', valor: (r) => r.rssPicoKb, tipo: 'kb' },
            { nombre: 'Swap del proceso pico', valor: (r) => r.swapPicoKb, tipo: 'kb' },
            { nombre: 'Fallos de página menores', valor: (r) => r.fallosMenores, tipo: 'num' },
            { nombre: 'Fallos de página mayores', valor: (r) => r.fallosMayores, tipo: 'num' },
            { nombre: 'CPU promedio', valor: (r) => r.cpuPorcentajePromedio, tipo: 'pct' },
            { nombre: 'Trabajo completado', valor: trabajoDe, tipo: 'num2' },
            {
                nombre: 'Contador final',
                valor: (r) => r.contadorFinal,
                tipo: 'num',
                soloSi: (r) => r.config.carga === 'sincronizacion'
            },
            {
                nombre: 'Actualizaciones perdidas',
                valor: (r) => r.actualizacionesPerdidas,
                tipo: 'num',
                soloSi: (r) => r.config.carga === 'sincronizacion'
            },
            { nombre: 'Páginas leídas de swap (pswpin)', valor: (r) => r.pswpin, tipo: 'num' },
            { nombre: 'Páginas escritas a swap (pswpout)', valor: (r) => r.pswpout, tipo: 'num' },
            { nombre: 'Límites', valor: textoLimites, texto: true }
        ];

        let html = '';
        for (const fila of filas) {
            if (fila.soloSi && !corridas.some(fila.soloSi)) continue;
            const celdas = corridas.map((corrida) => {
                if (fila.texto) {
                    return '<td>' + escaparHtml(String(fila.valor(corrida))) + '</td>';
                }
                return '<td class="numero">' +
                    escaparHtml(formatearValor(fila.tipo, fila.valor(corrida))) + '</td>';
            }).join('');
            html += '<tr><td>' + escaparHtml(fila.nombre) + '</td>' + celdas + '</tr>';
        }

        const referencia = corridas[0];
        html += '<tr class="separador"><td colspan="' + columnas + '">Deltas respecto de ' +
            escaparHtml(referencia.id) + '</td></tr>';

        const deltas = [
            { nombre: 'Δ duración', obtener: (r) => r.duracionMs, tipo: 'ms', mejor: 'menor' },
            { nombre: 'Δ RAM residente pico', obtener: (r) => r.rssPicoKb, tipo: 'kb', mejor: 'menor' },
            { nombre: 'Δ memoria virtual pico', obtener: (r) => r.vmPicoKb, tipo: 'kb', mejor: null },
            { nombre: 'Δ trabajo completado', obtener: trabajoDe, tipo: 'num2', mejor: 'mayor' }
        ];
        if (corridas.some((r) => r.config.carga === 'sincronizacion')) {
            deltas.push({
                nombre: 'Δ actualizaciones perdidas',
                obtener: (r) => r.actualizacionesPerdidas,
                tipo: 'num',
                mejor: 'menor'
            });
        }

        for (const delta of deltas) {
            const celdas = corridas.map((corrida, indice) => {
                if (indice === 0) return '<td class="delta">—</td>';
                const a = delta.obtener(referencia);
                const b = delta.obtener(corrida);
                if (!Number.isFinite(a) || !Number.isFinite(b)) return '<td class="delta">—</td>';
                const diferencia = b - a;
                const signo = diferencia > 0 ? '+' : '';
                return '<td class="delta ' + claseMejora(delta.mejor, diferencia) + '">' +
                    escaparHtml(signo + formatearValor(delta.tipo, diferencia)) + '</td>';
            }).join('');
            html += '<tr><td>' + escaparHtml(delta.nombre) + '</td>' + celdas + '</tr>';
        }

        const celdasVariacion = corridas.map((corrida, indice) => {
            if (indice === 0) return '<td class="delta">—</td>';
            if (!referencia.duracionMs) return '<td class="delta">—</td>';
            const variacion = ((corrida.duracionMs - referencia.duracionMs) /
                referencia.duracionMs) * 100;
            const signo = variacion > 0 ? '+' : '';
            return '<td class="delta ' + claseMejora('menor', variacion) + '">' +
                escaparHtml(signo + numero(variacion, 1) + ' %') + '</td>';
        }).join('');
        html += '<tr><td>Variación de duración</td>' + celdasVariacion + '</tr>';

        const puedeSpeedup = motivos.length === 0 &&
            new Set(corridas.map((r) => r.config.hilos)).size > 1;
        if (puedeSpeedup) {
            const celdasSpeedup = corridas.map((corrida, indice) => {
                if (indice === 0) return '<td class="delta">×1,00</td>';
                if (!corrida.duracionMs || !referencia.duracionMs) {
                    return '<td class="delta">—</td>';
                }
                const speedup = referencia.duracionMs / corrida.duracionMs;
                return '<td class="delta ' + claseMejora('mayor', speedup - 1) + '">×' +
                    escaparHtml(numero(speedup, 2)) + '</td>';
            }).join('');
            html += '<tr><td>Speedup (referencia / corrida)</td>' + celdasSpeedup + '</tr>';
        }

        $('cuerpoComparacion').innerHTML = html;
        renderBarras(corridas);
    }

    function renderBarras(corridas) {
        const contenedor = $('barrasComparacion');
        const maxDuracion = Math.max(1, ...corridas.map((r) => r.duracionMs));
        const trabajos = corridas.map(trabajoDe);
        const maxTrabajo = Math.max(1, ...trabajos);
        const esSincronizacion = corridas[0].config.carga === 'sincronizacion';

        const filasDuracion = corridas.map((corrida) => {
            const ancho = Math.max(0, Math.min(100, (corrida.duracionMs / maxDuracion) * 100));
            return '<div class="fila-barra">' +
                '<span class="etiqueta-barra" title="' + escaparHtml(corrida.id) + '">' +
                escaparHtml(corrida.id) + ' · ' + numero(corrida.config.hilos, 0) + ' h</span>' +
                '<span class="pista-barra"><span class="relleno-barra" style="width:' +
                ancho.toFixed(1) + '%"></span></span>' +
                '<span class="valor-barra">' + formatearDuracion(corrida.duracionMs) + '</span>' +
                '</div>';
        }).join('');

        const filasTrabajo = corridas.map((corrida, indice) => {
            const trabajo = trabajos[indice];
            const ancho = Math.max(0, Math.min(100, (trabajo / maxTrabajo) * 100));
            return '<div class="fila-barra">' +
                '<span class="etiqueta-barra" title="' + escaparHtml(corrida.id) + '">' +
                escaparHtml(corrida.id) + ' · ' + numero(corrida.config.hilos, 0) + ' h</span>' +
                '<span class="pista-barra"><span class="relleno-barra trabajo" style="width:' +
                ancho.toFixed(1) + '%"></span></span>' +
                '<span class="valor-barra">' + numero(trabajo, 2) + '</span>' +
                '</div>';
        }).join('');

        contenedor.innerHTML =
            '<div class="grupo-barras"><h4>Duración</h4>' + filasDuracion + '</div>' +
            '<div class="grupo-barras"><h4>' +
            (esSincronizacion ? 'Trabajo: contador final' : 'Trabajo: pasadas completadas') +
            '</h4>' + filasTrabajo + '</div>';
    }

    const NIVELES = {
        disponible: { icono: '✅', clase: 'verde', texto: 'disponible' },
        aproximado: { icono: '⚠️', clase: 'ambar', texto: 'aproximado' },
        no_disponible: { icono: '❌', clase: 'rojo', texto: 'no disponible' }
    };

    async function cargarCapacidades() {
        try {
            const datos = await fetchJson('/api/capacidades');
            estado.capacidades = Array.isArray(datos.capacidades) ? datos.capacidades : [];
            renderCapacidades();
        } catch (error) {
            mostrarMensaje('No se pudieron detectar las capacidades: ' + error.message, 'error');
        }
    }

    function renderCapacidades() {
        const cuerpo = $('cuerpoCapacidades');

        cuerpo.innerHTML = estado.capacidades.map((capacidad) => {
            const nivel = NIVELES[capacidad.nivel] || NIVELES.no_disponible;
            return '<tr>' +
                '<td>' + escaparHtml(capacidad.nombre) + '</td>' +
                '<td><code>' + escaparHtml(capacidad.mecanismo) + '</code></td>' +
                '<td><span class="insignia ' + nivel.clase + '">' + nivel.icono + ' ' +
                escaparHtml(nivel.texto) + '</span></td>' +
                '<td>' + escaparHtml(capacidad.motivo) + '</td>' +
                '</tr>';
        }).join('');

        if (!estado.capacidades.length) {
            cuerpo.innerHTML = '<tr><td colspan="4">No se informaron capacidades.</td></tr>';
        }
    }

    async function cargarSistema() {
        try {
            const sistema = await fetchJson('/api/sistema');
            estado.sistema = sistema;

            const swap = sistema.swapTotalKb > 0
                ? formatearKb(sistema.swapTotalKb - sistema.swapLibreKb) + ' de ' +
                  formatearKb(sistema.swapTotalKb)
                : 'no configurado';

            $('sistemaInfo').innerHTML =
                '<b>' + numero(sistema.nucleos, 0) + '</b> núcleos · ' +
                '<b>' + formatearKb(sistema.memTotalKb) + '</b> RAM · ' +
                '<b>' + formatearKb(sistema.memDisponibleKb) + '</b> disponible · swap ' + swap;
        } catch (error) {
            $('sistemaInfo').textContent = 'No se pudieron leer los datos del sistema.';
        }
    }

    async function cargarEscenarios() {
        try {
            const datos = await fetchJson('/api/escenarios');
            estado.escenarios = Array.isArray(datos.escenarios) ? datos.escenarios : [];

            const selector = $('selEscenario');
            const valorPrevio = selector.value;
            selector.innerHTML = '';

            for (const escenario of estado.escenarios) {
                const opcion = document.createElement('option');
                opcion.value = escenario.id;
                opcion.textContent = escenario.nombre;
                selector.appendChild(opcion);
            }

            const existe = estado.escenarios.some((escenario) => escenario.id === valorPrevio);
            const elegido = existe ? valorPrevio
                : (estado.escenarios[0] ? estado.escenarios[0].id : '');
            selector.value = elegido;
            if (elegido) aplicarEscenario(elegido);
        } catch (error) {
            mostrarMensaje('No se pudieron cargar los escenarios: ' + error.message, 'error');
        }
    }

    async function iniciar() {
        configurarPestanas();
        configurarFormulario();
        configurarBotones();
        configurarResultados();
        crearGraficos();
        limpiarTarjetas();
        actualizarBadgeLimites(null, null);
        actualizarBotones();

        await Promise.all([
            cargarSistema(),
            cargarCapacidades(),
            cargarEscenarios(),
            cargarResultados()
        ]);

        abrirFuente();

        try {
            const actual = await fetchJson('/api/corrida/estado');
            if (actual && actual.activo) {
                aplicarEstado(actual);
                cambiarPestana('vivo');
            }
        } catch (error) {
        }
    }

    iniciar();
})();