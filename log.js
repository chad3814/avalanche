/**
 * (c) Chad Walker, Chris Kirmse
 */

/* tslint:disable */
export class Log {
  trace = console.trace;
  info = console.info;
  warn = console.warn;
  error = console.error;
}

export const defaultLogger = new Log();

let currentLogger = defaultLogger;

export function setLogger(logger) {
  currentLogger = logger;
}

export default {
  trace: (...args) => currentLogger.trace(...args),
  info: (...args) => currentLogger.info(...args),
  warn: (...args) => currentLogger.warn(...args),
  error: (...args) => currentLogger.error(...args),
};
