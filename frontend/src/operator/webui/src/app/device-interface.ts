export class Device {
  private id = '';

  constructor(id: string) {
    this.id = id;
  }

  get deviceId(): string {
    return this.id;
  }
}
