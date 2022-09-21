import {Pipe, PipeTransform} from '@angular/core';
import {DomSanitizer, SafeResourceUrl} from '@angular/platform-browser';

@Pipe({
  name: 'safedeviceurl',
})
export class SafeDeviceUrlPipe implements PipeTransform {
  constructor(private sanitizer: DomSanitizer) {}

  transform(deviceId: string): SafeResourceUrl {
    return this.sanitizer.bypassSecurityTrustResourceUrl(
      `/devices/${deviceId}/files/client.html`
    );
  }
}
