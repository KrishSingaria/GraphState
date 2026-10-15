import { useState } from 'react';
import type { Config } from './config';

export interface User {
  id: string;
  name: string;
  email?: string;
}

export class UserService {
  private serviceName: string;

  constructor(name: string) {
    this.serviceName = name;
  }

  public async fetchUser(id: string): Promise<User> {
    return { id, name: "Alice" };
  }

  public static createDefault(): UserService {
    return new UserService("default");
  }
}

// Function with overload
export function processItem(x: number): number;
export function processItem(x: string): string;
export function processItem(x: any): any {
  return x;
}

export const formatUserName = (user: User, uppercase?: boolean): string => {
  if (uppercase) {
    return user.name.toUpperCase();
  }
  return user.name;
};

export function main(): void {
  const service = new UserService("test");
  service.fetchUser("123");
  formatUserName({ id: "1", name: "Bob" });
}
