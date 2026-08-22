const SECRET_NAME = /(authorization|cookie|credential|password|passwd|secret|token|private[_-]?key)/i;
const SENSITIVE_VALUE = [
  /\b(Bearer|Basic)\s+[A-Za-z0-9._~+\/-]+=*/gi,
  /\b(?:ami|ari)_[A-Za-z0-9._~-]{8,}\b/gi,
  /(?:password|passwd|secret|token)\s*[=:]\s*[^\s,;]+/gi,
] as const;

export interface RedactionOptions {
  extraValues?: ReadonlyArray<string>;
}

export function redactText(value: string, options: RedactionOptions = {}): string {
  let output = value;
  for (const pattern of SENSITIVE_VALUE) output = output.replace(pattern, "[REDACTED]");
  for (const extra of options.extraValues ?? []) {
    if (extra.length > 0) output = output.split(extra).join("[REDACTED]");
  }
  return output;
}

export function redactRecord(
  value: Readonly<Record<string, string | undefined>>,
): Readonly<Record<string, string>> {
  return Object.fromEntries(
    Object.entries(value).map(([key, entry]) => [
      key,
      SECRET_NAME.test(key) ? "[REDACTED]" : redactText(entry ?? ""),
    ]),
  );
}

export function assertNoSecretArguments(args: ReadonlyArray<string>): void {
  for (const argument of args) {
    if (SECRET_NAME.test(argument) || SENSITIVE_VALUE.some((pattern) => {
      pattern.lastIndex = 0;
      return pattern.test(argument);
    })) {
      throw new Error("Secret-bearing command arguments are prohibited");
    }
  }
}
